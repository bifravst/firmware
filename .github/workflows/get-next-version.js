const {getTags} = require('semantic-release/lib/git')
const {escapeRegExp, template} = require('lodash');
const { promises: fs } = require('fs')
const path = require('path')
const semver = require('semver');

const tagFormat = `v\${version}`
const tagRegexp = `^${escapeRegExp(template(tagFormat)({version: ' '})).replace(' ', '(.+)')}`;

const main = async () => {
  const pjson = JSON.parse(await fs.readFile(path.join(process.cwd(), 'package.json')))

  const tags = (await getTags())
      .map(tag => ({gitTag: tag, version: (tag.match(tagRegexp) || new Array(2))[1]}))
      .filter(
          tag => tag.version && semver.valid(semver.clean(tag.version)) && !semver.prerelease(semver.clean(tag.version))
      )
      .sort((a, b) => semver.rcompare(a.version, b.version));
  const latestTag = tags[0]
  const nextVersion = semver.inc(latestTag.version, "minor")
  process.stdout.write(template(tagFormat)({version: nextVersion}))
}

main()
