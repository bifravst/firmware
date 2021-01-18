import {
	FeatureRunner,
	ConsoleReporter,
	awsSdkStepRunners,
	storageStepRunners,
} from '@bifravst/e2e-bdd-test-runner'
import * as program from 'commander'
import * as chalk from 'chalk'
import { bifravstStepRunners, certsDir } from '@bifravst/aws'
import { IoTClient } from '@aws-sdk/client-iot'
import { STSClient, GetCallerIdentityCommand } from '@aws-sdk/client-sts'
import { promises as fs } from 'fs'
import * as path from 'path'
import { firmwareCIStepRunners } from './steps/firmwareCI'
import { downloadDeviceCredentials } from './lib/downloadDeviceCredentials'
import { fromEnv } from '../util/fromEnv'

let ran = false

const {
	stackName,
	region,
	mqttEndpoint,
	accessKeyId,
	secretAccessKey,
} = fromEnv({
	accessKeyId: 'AWS_ACCESS_KEY_ID',
	secretAccessKey: 'AWS_SECRET_ACCESS_KEY',
	stackName: 'STACK_NAME',
	region: 'AWS_REGION',
	mqttEndpoint: 'BROKER_HOSTNAME',
})(process.env)

const testEnvSDKConfig = {
	region,
	credentials: {
		accessKeyId,
		secretAccessKey,
	},
}

program
	.arguments('<featureDir>')
	.option('-r, --print-results', 'Print results')
	.option('-p, --progress', 'Print progress')
	.action(
		async (
			featureDir: string,
			options: {
				printResults: boolean
				progress: boolean
				retry: boolean
			},
		) => {
			ran = true
			const { printResults, progress } = options

			const { Account: accountId } = await new STSClient(testEnvSDKConfig).send(
				new GetCallerIdentityCommand({}),
			)

			const c = await certsDir({
				iotEndpoint: mqttEndpoint,
				accountId: accountId as string,
			})

			const iot = new IoTClient(testEnvSDKConfig)

			const world = {
				region: region,
				accountId: accountId as string,
				awsIotRootCA: await fs.readFile(
					path.join(process.cwd(), 'ci', 'data', 'AmazonRootCA1.pem'),
					'utf-8',
				),
				certsDir: c,
				mqttEndpoint,
				stackName,
				...fromEnv({
					env__JOB_ID: 'JOB_ID',
					env__AWS_ACCESS_KEY_ID: 'AWS_ACCESS_KEY_ID',
					env__AWS_SECRET_ACCESS_KEY: 'AWS_SECRET_ACCESS_KEY',
					env__CAT_TRACKER_APP_VERSION: 'CAT_TRACKER_APP_VERSION',
				})(process.env),
			}

			console.log(chalk.yellow.bold(' World:'))
			console.log()
			console.log(world)
			console.log()

			await downloadDeviceCredentials({
				certsDir: c,
				...fromEnv({ deviceId: 'JOB_ID' })(process.env),
				iot,
				brokerHostname: mqttEndpoint,
			})
			console.log()

			const runner = new FeatureRunner<typeof world>(world, {
				dir: featureDir,
				reporters: [
					new ConsoleReporter({
						printResults,
						printProgress: progress,
						printSummary: true,
					}),
				],
				retry: false,
			})

			try {
				const { success } = await runner
					.addStepRunners(
						firmwareCIStepRunners({
							iot,
						}),
					)
					.addStepRunners(
						awsSdkStepRunners({
							constructorArgs: {
								IotData: {
									endpoint: world.mqttEndpoint,
								},
							},
						}),
					)
					.addStepRunners(bifravstStepRunners(world))
					.addStepRunners(storageStepRunners())
					.run()
				if (!success) {
					process.exit(1)
					return
				}
				process.exit()
			} catch (error) {
				console.error(chalk.red('Running the features failed!'))
				console.error(error)
				process.exit(1)
			}
		},
	)
	.parse(process.argv)

if (!ran) {
	program.outputHelp()
	process.exit(1)
}
