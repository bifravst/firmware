import * as chalk from 'chalk'
import { promises as fs } from 'fs'
import {
	certsDir as provideCertsDir,
	deviceFileLocations,
	caFileLocations,
	createCA,
	createDeviceCertificate,
} from '@bifravst/aws'
import {
	IoTClient,
	DescribeThingCommand,
	CreateJobCommand,
} from '@aws-sdk/client-iot'
import { STSClient, GetCallerIdentityCommand } from '@aws-sdk/client-sts'
import {
	S3Client,
	DeleteObjectCommand,
	PutObjectCommand,
} from '@aws-sdk/client-s3'
import { CloudFormationClient } from '@aws-sdk/client-cloudformation'
import {
	IoTDataPlaneClient,
	GetThingShadowCommand,
} from '@aws-sdk/client-iot-data-plane'
import { schedule, wait } from '@bifravst/firmware-ci-aws'
import * as path from 'path'
import fetch from 'node-fetch'
import { v4 } from 'uuid'
import { fromEnv } from './util/fromEnv'
import { TextDecoder } from 'util'

const target = 'nrf9160dk_nrf9160ns'
const network = 'ltem'
const secTag = 42
const timeoutInMinutes = 10

const { jobId, appVersion } = fromEnv({
	jobId: 'JOB_ID',
	appVersion: 'CAT_TRACKER_APP_VERSION',
})(process.env)

const hexFile = process.env.HEX_FILE ?? path.join(process.cwd(), 'firmware.hex')
const fotaFile =
	process.env.FOTA_FILE ?? path.join(process.cwd(), 'fota-upgrade.bin')

const firmwareCI = fromEnv({
	accessKeyId: 'FIRMWARECI_AWS_ACCESS_KEY_ID',
	secretAccessKey: 'FIRMWARECI_AWS_SECRET_ACCESS_KEY',
	bucketName: 'FIRMWARECI_BUCKET_NAME',
	region: 'FIRMWARECI_AWS_REGION',
	deviceId: 'FIRMWARECI_DEVICE_ID',
})(process.env)

const firmwareCISDKConfig = {
	region: firmwareCI.region,
	credentials: {
		accessKeyId: firmwareCI.accessKeyId,
		secretAccessKey: firmwareCI.secretAccessKey,
	},
}

const testEnv = fromEnv({
	accessKeyId: 'TESTENV_AWS_ACCESS_KEY_ID',
	secretAccessKey: 'TESTENV_AWS_SECRET_ACCESS_KEY',
	region: 'TESTENV_AWS_REGION',
	endpoint: 'TESTENV_BROKER_HOSTNAME',
	stackName: 'TESTENV_STACK_NAME',
})(process.env)

const testEnvSDKConfig = {
	region: testEnv.region,
	credentials: {
		accessKeyId: testEnv.accessKeyId,
		secretAccessKey: testEnv.secretAccessKey,
	},
}

const e2e = async () => {
	const { Account: TestAccount } = await new STSClient(testEnvSDKConfig).send(
		new GetCallerIdentityCommand({}),
	)

	if (TestAccount === undefined)
		throw new Error(`Could not authenticate against test environment!`)

	const certsDir = await provideCertsDir({
		accountId: TestAccount,
		iotEndpoint: testEnv.endpoint,
	})

	console.error(
		chalk.yellow('Test Env / Account:       '),
		chalk.blue(TestAccount),
	)
	console.error(
		chalk.yellow('Test Env / Region:        '),
		chalk.blue(testEnv.region),
	)
	console.error(
		chalk.yellow('Test Env / Stack:         '),
		chalk.blue(testEnv.stackName),
	)
	console.error(
		chalk.yellow('Test Env / IoT Endpoint:  '),
		chalk.blue(testEnv.endpoint),
	)
	console.error(
		chalk.yellow('Test Env / Certificates:  '),
		chalk.blue(certsDir),
	)

	const { Account: CIAccount } = await new STSClient(firmwareCISDKConfig).send(
		new GetCallerIdentityCommand({}),
	)
	const ciDeviceArn = `arn:aws:iot:${firmwareCI.region}:${CIAccount}:thing/${firmwareCI.deviceId}`

	console.error(
		chalk.yellow('Firmware CI / Account:    '),
		chalk.blue(CIAccount),
	)
	console.error(
		chalk.yellow('Firmware CI / Region:     '),
		chalk.blue(firmwareCI.region),
	)
	console.error(
		chalk.yellow('Firmware CI / Bucket:     '),
		chalk.blue(firmwareCI.bucketName),
	)
	console.error(
		chalk.yellow('Firmware CI / Device Arn: '),
		chalk.blue(ciDeviceArn),
	)

	console.error(chalk.yellow('Job / ID:                 '), chalk.blue(jobId))

	const iot = new IoTClient(firmwareCISDKConfig)

	let jobInfo
	// Job exists?
	try {
		jobInfo = await wait({
			iot,
			jobId,
			interval: 10,
		})
	} catch {
		console.error(chalk.magenta('Uploading firmware...'))
		const s3 = new S3Client(firmwareCISDKConfig)
		const fotaFilename = `${jobId.substr(0, 8)}.bin`
		await Promise.all([
			s3.send(
				new PutObjectCommand({
					Bucket: firmwareCI.bucketName,
					Key: `${jobId}.hex`,
					Body: await fs.readFile(hexFile),
					ContentType: 'text/octet-stream',
				}),
			),
			s3.send(
				new PutObjectCommand({
					Bucket: firmwareCI.bucketName,
					Key: fotaFilename,
					Body: await fs.readFile(fotaFile),
					ContentType: 'text/octet-stream',
				}),
			),
		])

		const ca = caFileLocations(certsDir)

		try {
			await fs.stat(ca.id)
		} catch {
			console.error(chalk.magenta('Generating CA certificate...'))
			await createCA({
				certsDir,
				iot: new IoTClient(testEnvSDKConfig),
				cf: new CloudFormationClient(testEnvSDKConfig),
				stack: testEnv.stackName,
				subject: `firmware-ci-${v4()}`,
				log: console.error,
				debug: console.debug,
			})
		}

		await createDeviceCertificate({
			certsDir,
			deviceId: jobId,
			mqttEndpoint: testEnv.endpoint,
			awsIotRootCA: await fs.readFile(
				path.resolve(process.cwd(), 'ci', 'data', 'AmazonRootCA1.pem'),
				'utf-8',
			),
		})

		const deviceCert = deviceFileLocations({
			certsDir,
			deviceId: jobId,
		})

		// Writes the JSON file which works with the Certificate Manager of the LTA Link Monitor
		await fs.writeFile(
			deviceCert.json,
			JSON.stringify(
				{
					caCert: await fs.readFile(
						path.resolve(process.cwd(), 'ci', 'data', 'AmazonRootCA1.pem'),
						'utf-8',
					),
					clientCert: await fs.readFile(deviceCert.certWithCA, 'utf-8'),
					privateKey: await fs.readFile(deviceCert.key, 'utf-8'),
					clientId: jobId,
					brokerHostname: testEnv.endpoint,
				},
				null,
				2,
			),
			'utf-8',
		)

		const jobDocument = await schedule({
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
			timeoutInMinutes,
			abortOn: [`aws_fota: Error (-7) when trying to start firmware download`],
			endOn: [
				`Version:     ${appVersion}-upgraded`,
				// Wait for the shadow update
				`"appV": "${appVersion}-upgraded"`,
				'MQTT_EVT_SUBACK',
			],
		})

		await fs.writeFile(
			'jobDocument.json',
			JSON.stringify(jobDocument, null, 2),
			'utf-8',
		)
		console.error(
			chalk.magenta('Stored job document in'),
			chalk.blueBright('jobDocument.json'),
		)

		// Inject behaviour once the device connects
		const iotDataTestEnv = new IoTDataPlaneClient({
			...testEnvSDKConfig,
			endpoint: testEnv.endpoint,
		})
		const iotTestEnv = new IoTClient(testEnvSDKConfig)
		let timeLeft = timeoutInMinutes * 60 - 60
		const scheduleFOTA = async () => {
			process.stderr.write(
				chalk.magenta(`Checking if device has connected ... `),
			)

			const reschedule = () => {
				timeLeft -= 10
				if (timeLeft > 0) {
					setTimeout(scheduleFOTA, 10 * 1000)
				} else {
					console.error(
						chalk.red(
							'Device did not connect within ${timeoutInMinutes} minutes.',
						),
					)
				}
			}

			try {
				const shadow = await iotDataTestEnv.send(
					new GetThingShadowCommand({
						thingName: jobId,
					}),
				)
				const { state } = JSON.parse(
					new TextDecoder('utf-8').decode(shadow.payload),
				)
				console.error(chalk.green(`Device has connected.`))
				if (state?.reported?.dev === undefined) {
					console.error(
						chalk.red(`Device has not reported device information, yet.`),
					)
					reschedule()
					return
				}
				// Schedule FOTA job
				const { thingArn } = await iotTestEnv.send(
					new DescribeThingCommand({
						thingName: jobId,
					}),
				)
				if (thingArn === undefined)
					throw new Error(`Failed to describe thing ${jobId}!`)

				const stat = await fs.stat(fotaFile)
				const fotaJobDocument = {
					operation: 'app_fw_update',
					size: stat.size,
					filename: fotaFilename,
					location: {
						protocol: 'https',
						host: `${firmwareCI.bucketName}.s3-${firmwareCI.region}.amazonaws.com`,
						path: fotaFilename,
					},
					fwversion: `${appVersion}-upgraded`,
					targetBoard: '9160DK',
				}
				await fs.writeFile(
					'fotaJobDocument.json',
					JSON.stringify(fotaJobDocument, null, 2),
					'utf-8',
				)
				console.error(
					chalk.magenta('Stored FOTA job document in'),
					chalk.blueBright('fotaJobDocument.json'),
				)
				const job = await iotTestEnv.send(
					new CreateJobCommand({
						jobId: v4(),
						targets: [thingArn],
						document: JSON.stringify(fotaJobDocument),
						description: `Upgrade ${
							thingArn.split('/')[1]
						} to version ${appVersion}-upgraded.`,
						targetSelection: 'SNAPSHOT',
					}),
				)
				console.error(chalk.green(`FOTA job created.`))
				console.log({ job })
			} catch (err) {
				console.error(chalk.red(`Device has not connected, yet.`))
				console.error(chalk.red(err.message))
				reschedule()
			}
		}
		setTimeout(scheduleFOTA, 60 * 1000)

		// Wait for the job to complete
		jobInfo = await wait({
			iot,
			jobId,
			interval: 10,
			timeoutInMinutes: timeoutInMinutes * 2,
		})

		// Delete
		await Promise.all([
			s3.send(
				new DeleteObjectCommand({
					Bucket: firmwareCI.bucketName,
					Key: `${jobId}.hex`,
				}),
			),
			s3.send(
				new DeleteObjectCommand({
					Bucket: firmwareCI.bucketName,
					Key: fotaFilename,
				}),
			),
		])
	}

	const report = JSON.parse(
		await (await fetch(jobInfo.jobDocument.reportUrl)).text(),
	)
	await fs.writeFile('report.json', JSON.stringify(report, null, 2), 'utf-8')
	console.error(
		chalk.magenta('Stored report in'),
		chalk.blueBright('report.json'),
	)
	const { result, flashLog, deviceLog } = report
	console.log()
	console.log('** Result **')
	console.log()
	console.log(result)
	console.log()
	console.log('** Flash Log **')
	console.log()
	console.log(flashLog.join('\n'))
	console.log()
	console.log('** Device Log **')
	console.log()
	console.log(deviceLog.join('\n'))
}

void e2e()
