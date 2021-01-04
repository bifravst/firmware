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

const { stackName, testEnvRegion, mqttEndpoint } = fromEnv({
	stackName: 'TESTENV_STACK_NAME',
	testEnvRegion: 'TESTENV_AWS_REGION',
	mqttEndpoint: 'TESTENV_BROKER_HOSTNAME',
})(process.env)

const testEnvCredentials = fromEnv({
	accessKeyId: 'TESTENV_AWS_ACCESS_KEY_ID',
	secretAccessKey: 'TESTENV_AWS_SECRET_ACCESS_KEY',
})(process.env)

const firmwareCICredentials = fromEnv({
	accessKeyId: 'FIRMWARECI_AWS_ACCESS_KEY_ID',
	secretAccessKey: 'FIRMWARECI_AWS_SECRET_ACCESS_KEY',
})(process.env)

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

			const { Account: accountId } = await new STSClient({
				region: testEnvRegion,
				credentials: testEnvCredentials,
			}).send(new GetCallerIdentityCommand({}))

			const c = await certsDir({
				iotEndpoint: mqttEndpoint,
				accountId: accountId as string,
			})

			const iot = new IoTClient({
				region: testEnvRegion,
				credentials: firmwareCICredentials,
			})

			const world = {
				region: testEnvRegion,
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
					env__TESTENV_AWS_ACCESS_KEY_ID: 'TESTENV_AWS_ACCESS_KEY_ID',
					env__TESTENV_AWS_SECRET_ACCESS_KEY: 'TESTENV_AWS_SECRET_ACCESS_KEY',
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
	program.outputHelp(chalk.red)
	process.exit(1)
}
