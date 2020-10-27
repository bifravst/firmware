const chalk = require("chalk");
const { promises: fs } = require("fs");
const {
  certsDir: provideCertsDir,
  deviceFileLocations,
  caFileLocations,
  createCA,
  createDeviceCertificate,
} = require("@bifravst/aws");
const { Iot, STS, S3, CloudFormation } = require("aws-sdk");
const { schedule, wait } = require("@bifravst/firmware-ci");
const path = require("path");
const fetch = require("node-fetch");

const target = "thingy91_nrf9160ns";
const network = "ltem";
const secTag = 42;

const jobId = process.env.JOB_ID;

const hexFile =
  process.env.HEX_FILE ?? path.join(process.cwd(), "build/zephyr/merged.hex");

const firmwareCI = {
  accessKeyId: process.env.FIRMWARECI_AWS_ACCESS_KEY_ID,
  secretAccessKey: process.env.FIRMWARECI_AWS_SECRET_ACCESS_KEY,
  bucketName: process.env.FIRMWARECI_BUCKET_NAME,
  region: process.env.FIRMWARECI_REGION,
  deviceId: process.env.FIRMWARECI_DEVICE_ID,
};

const firmwareCISDKConfig = {
  region: firmwareCI.region,
  credentials: {
    accessKeyId: firmwareCI.accessKeyId,
    secretAccessKey: firmwareCI.secretAccessKey,
  },
};

const testEnv = {
  accessKeyId: process.env.TESTENV_AWS_ACCESS_KEY_ID,
  secretAccessKey: process.env.TESTENV_AWS_SECRET_ACCESS_KEY,
  region: process.env.TESTENV_AWS_DEFAULT_REGION,
  endpoint: process.env.TEST_ENV_BROKER_HOSTNAME,
  stackName: process.env.TESTENV_STACK_NAME,
};

const testEnvSDKConfig = {
  region: testEnv.region,
  credentials: {
    accessKeyId: testEnv.accessKeyId,
    secretAccessKey: testEnv.secretAccessKey,
  },
};

/*

- Create credentials for the test device
- Schedule a Firmware CI run
- Wait for the completion of the CI run
- Download the logs
- Check for the correct device behaviour
- Delete the test device
*/

const e2e = async () => {
  const { Account: TestAccount } = await new STS(testEnvSDKConfig)
    .getCallerIdentity()
    .promise();

  const certsDir = await provideCertsDir({
    accountId: TestAccount,
    iotEndpoint: testEnv.endpoint,
  });

  console.error(
    chalk.yellow("Test Env / Account:       "),
    chalk.blue(TestAccount)
  );
  console.error(
    chalk.yellow("Test Env / Region:        "),
    chalk.blue(testEnv.region)
  );
  console.error(
    chalk.yellow("Test Env / Stack:         "),
    chalk.blue(testEnv.stackName)
  );
  console.error(
    chalk.yellow("Test Env / IoT Endpoint:  "),
    chalk.blue(testEnv.endpoint)
  );
  console.error(
    chalk.yellow("Test Env / Certificates:  "),
    chalk.blue(certsDir)
  );

  const { Account: CIAccount } = await new STS(firmwareCISDKConfig)
    .getCallerIdentity()
    .promise();
  const ciDeviceArn = `arn:aws:iot:${firmwareCI.region}:${CIAccount}:thing/${firmwareCI.deviceId}`;

  console.error(
    chalk.yellow("Firmware CI / Account:    "),
    chalk.blue(CIAccount)
  );
  console.error(
    chalk.yellow("Firmware CI / Region:     "),
    chalk.blue(firmwareCI.region)
  );
  console.error(
    chalk.yellow("Firmware CI / Bucket:     "),
    chalk.blue(firmwareCI.bucketName)
  );
  console.error(
    chalk.yellow("Firmware CI / Device Arn: "),
    chalk.blue(ciDeviceArn)
  );

  console.error(chalk.yellow("Job / ID:                 "), chalk.blue(jobId));

  const iot = new Iot(firmwareCISDKConfig);

  let jobInfo;
  // Job exists?
  try {
    jobInfo = await wait({
      iot,
      jobId,
      interval: 10,
    });
  } catch {
    console.error(chalk.magenta("Uploading firmware..."));
    const s3 = new S3(firmwareCISDKConfig);
    await s3
      .putObject({
        Bucket: firmwareCI.bucketName,
        Key: `${jobId}.hex`,
        Body: await fs.readFile(hexFile),
        ContentType: "text/octet-stream",
      })
      .promise();

    const ca = caFileLocations(certsDir);

    try {
      await fs.stat(ca.id);
    } catch {
      console.error(chalk.magenta("Generating CA certificate..."));
      await createCA({
        certsDir,
        iot: new Iot(testEnvSDKConfig),
        cf: new CloudFormation(testEnvSDKConfig),
        stack: testEnv.stackName,
        subject: "firmware-ci",
        log: console.error,
        debug: console.debug,
      });
    }

    await createDeviceCertificate({
      certsDir,
      deviceId: jobId,
    });

    const deviceCert = deviceFileLocations({
      certsDir,
      deviceId: jobId,
    });

    // Writes the JSON file which works with the Certificate Manager of the LTA Link Monitor
    await fs.writeFile(
      deviceCert.json,
      JSON.stringify(
        {
          caCert: await fs.readFile(
            path.resolve(process.cwd(), "data", "AmazonRootCA1.pem"),
            "utf-8"
          ),
          clientCert: await fs.readFile(deviceCert.certWithCA, "utf-8"),
          privateKey: await fs.readFile(deviceCert.key, "utf-8"),
          clientId: jobId,
          brokerHostname: testEnv.endpoint,
        },
        null,
        2
      ),
      "utf-8"
    );

    await schedule({
      bucketName: firmwareCI.bucketName,
      certificateJSON: deviceCert.json,
      ciDeviceArn,
      firmwareUrl: `https://${firmwareCI.bucketName}.s3.${firmwareCI.region}.amazonaws.com/${jobId}.hex`,
      network,
      secTag,
      region: firmwareCI.region,
      s3,
      target,
      iot,
      jobId,
    });

    jobInfo = await wait({
      iot,
      jobId,
      interval: 10,
    });
  }

  const { result, flashLog, deviceLog, connections } = JSON.parse(
    await (await fetch(jobInfo.jobDocument.reportUrl)).text()
  );
  console.log();
  console.log("** Result **");
  console.log();
  console.log(result);
  console.log();
  console.log("** Connections **");
  console.log();
  console.log(connections);
  console.log();
  console.log("** Flash Log **");
  console.log();
  console.log(flashLog.join("\n"));
  console.log();
  console.log("** Device Log **");
  console.log();
  console.log(deviceLog.join("\n"));
};

void e2e();
