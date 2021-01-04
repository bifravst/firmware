import { deviceFileLocations } from '@bifravst/aws'
import { promises as fs } from 'fs'
import * as chalk from 'chalk'
import { IoTClient, GetJobDocumentCommand } from '@aws-sdk/client-iot'

export const downloadDeviceCredentials = async ({
	certsDir,
	deviceId,
	iot,
	brokerHostname,
}: {
	certsDir: string
	deviceId: string
	iot: IoTClient
	brokerHostname: string
}) => {
	const deviceFiles = deviceFileLocations({
		certsDir,
		deviceId,
	})
	console.log(
		chalk.magenta('Checking device credentials...'),
		chalk.blueBright(deviceFiles.json),
	)
	try {
		await fs.stat(deviceFiles.json)
		console.log(chalk.green('Device credentials exist locally'))
		return
	} catch {
		console.log(chalk.red('Device credentials do not exist locally.'))
		console.log(chalk.magenta('Downloading from job...'))

		const { document } = await iot.send(
			new GetJobDocumentCommand({
				jobId: deviceId,
			}),
		)

		const d = JSON.parse(document as string)

		await fs.writeFile(
			deviceFiles.json,
			JSON.stringify(
				{
					...d.credentials,
					clientId: deviceId,
					brokerHostname,
				},
				null,
				2,
			),
			'utf-8',
		)
		console.log(chalk.green('Written device credentials'))
	}
}
