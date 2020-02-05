const semanticRelease = require('semantic-release')
const { promises: fs } = require('fs')
const path = require('path')
const { WritableStreamBuffer } = require('stream-buffers')

const stdoutBuffer = new WritableStreamBuffer()
const stderrBuffer = new WritableStreamBuffer()

const main = async () => {
  const pjson = JSON.parse(await fs.readFile(path.join(process.cwd(), 'package.json')))
  const result = await semanticRelease({
    // Core options
    branch: pjson.release.branch,
    repositoryUrl: pjson.repository.url,
    plugins: [
      '@semantic-release/commit-analyzer'
    ],
    dryRun: true
  }, {
    cwd: process.cwd(),
    stdout: stdoutBuffer,
    stderr: stderrBuffer
  })

  if (result) {
    const { nextRelease } = result
    process.stdout.write(nextRelease.version)
  } else {
    console.error('No new release.')
    process.stderr.write(stdoutBuffer.getContentsAsString('utf8'))
    if (stderrBuffer.size > 0) {
      process.stderr.write(stderrBuffer.getContentsAsString('utf8'))
    }
    process.stdout.write(pjson.version)
  }
}

main()
