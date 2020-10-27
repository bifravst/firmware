import {
	FeatureRunner,
	ConsoleReporter,
	awsSdkStepRunners,
	storageStepRunners,
} from '@bifravst/e2e-bdd-test-runner'
import * as program from 'commander'
import * as chalk from 'chalk'
import { bifravstStepRunners, certsDir } from '@bifravst/aws'
import { Iot, STS } from 'aws-sdk'
import { promises as fs } from 'fs'
import * as path from 'path'
import { firmwareCIStepRunners } from './steps/firmwareCI'

let ran = false

const stackName = process.env.TESTENV_STACK_NAME as string
const region = process.env.TESTENV_AWS_DEFAULT_REGION as string
const mqttEndpoint = process.env.TEST_ENV_BROKER_HOSTNAME as string
const testEnvCredentials = {
	accessKeyId: process.env.TESTENV_AWS_ACCESS_KEY_ID as string,
	secretAccessKey: process.env.TESTENV_AWS_SECRET_ACCESS_KEY as string,
}
const firmwareCICredentials = {
	accessKeyId: process.env.FIRMWARECI_AWS_ACCESS_KEY_ID as string,
	secretAccessKey: process.env.FIRMWARECI_AWS_SECRET_ACCESS_KEY as string,
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

			const { Account: accountId } = await new STS({
				region,
				credentials: testEnvCredentials,
			})
				.getCallerIdentity()
				.promise()

			const world = {
				region,
				accountId: accountId as string,
				awsIotRootCA: await fs.readFile(
					path.join(process.cwd(), 'data', 'AmazonRootCA1.pem'),
					'utf-8',
				),
				certsDir: await certsDir({
					iotEndpoint: mqttEndpoint,
					accountId: accountId as string,
				}),
				mqttEndpoint,
				stackName,
				env__JOB_ID: process.env.JOB_ID,
				env__TESTENV_AWS_ACCESS_KEY_ID: process.env.TESTENV_AWS_ACCESS_KEY_ID,
				env__TESTENV_AWS_SECRET_ACCESS_KEY:
					process.env.TESTENV_AWS_SECRET_ACCESS_KEY,
				env__NEXT_VERSION: process.env.NEXT_VERSION,
			}

			console.log(chalk.yellow.bold(' World:'))
			console.log()
			console.log(world)
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
							iot: new Iot({ region, credentials: firmwareCICredentials }),
						}),
					)
					.addStepRunners(
						awsSdkStepRunners({
							region: world.region,
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
