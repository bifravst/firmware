import {
	regexGroupMatcher,
	StepRunnerFunc,
	InterpolatedStep,
} from '@bifravst/e2e-bdd-test-runner'
import { wait } from '@bifravst/firmware-ci'
import { FirmwareCIJobDocument } from '@bifravst/firmware-ci/dist/runner/job'
import { Iot } from 'aws-sdk'
import fetch from 'node-fetch'

export const firmwareCIStepRunners = ({
	iot,
}: {
	iot: Iot
}): ((step: InterpolatedStep) => StepRunnerFunc<any> | false)[] => {
	const jobs: Record<
		string,
		Promise<{
			job: Iot.Job
			jobDocument: FirmwareCIJobDocument
		}>
	> = {}

	return [
		regexGroupMatcher<any>(
			/^the Firmware CI job "(?<jobId>[^"]+)" has completed$/,
		)(async ({ jobId }, _, runner) => {
			if (jobs[jobId] !== undefined) return
			jobs[jobId] = wait({
				iot,
				jobId,
			})
			const { job, jobDocument } = await jobs[jobId]
			const { result, flashLog, deviceLog, connections } = JSON.parse(
				await (await fetch(jobDocument.reportUrl)).text(),
			)
			runner.store[`firmwareci:${jobId}:job`] = job
			runner.store[`firmwareci:${jobId}:document`] = jobDocument
			runner.store[`firmwareci:${jobId}:result`] = result
			runner.store[`firmwareci:${jobId}:flashLog`] = flashLog
			runner.store[`firmwareci:${jobId}:deviceLog`] = deviceLog
			runner.store[`firmwareci:${jobId}:connections`] = connections
		}),
		regexGroupMatcher<any>(
			/^the Firmware CI device log for job "(?<jobId>[^"]+)" should contain$/,
		)(async ({ jobId }, step, runner) => {
			if (step.interpolatedArgument === undefined) {
				throw new Error('Must provide argument!')
			}
			const expected = step.interpolatedArgument
				.split('\n')
				.map((s) => s.trim())

			const matches = expected.map((e) => ({
				matches: (runner.store[
					`firmwareci:${jobId}:deviceLog`
				] as string[]).filter((s) => s.includes(e)),
				expected: e,
			}))

			matches.map(({ matches, expected }) => {
				if (matches.length === 0)
					throw new Error(`deviceLog did not contain "${expected}"`)
			})

			return matches.map(({ matches }) => matches).flat()
		}),
	]
}
