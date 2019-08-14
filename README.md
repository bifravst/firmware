# Cat Tracker Firmware

[![CircleCI](https://circleci.com/gh/bifravst/cat-tracker-fw/tree/saga.svg?style=svg)](https://circleci.com/gh/bifravst/cat-tracker-fw/tree/saga)
[![semantic-release](https://img.shields.io/badge/%20%20%F0%9F%93%A6%F0%9F%9A%80-semantic--release-e10079.svg)](https://github.com/semantic-release/semantic-release)
[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)

Read the documentation at Read the documentation at https://bifravst.github.io/.

This application is built with [fw-nrfconnect-nrf](https://github.com/NordicPlayground/fw-nrfconnect-nrf).

Supported boards:

- nRF9160 DK (PCA10090) (`west build -b nrf9160_pca10090ns`)
- Thingy:91 (PCA20035) (`west build -b nrf9160_pca20035ns`)

## Automated releases

This project uses [Semantic Release](https://github.com/semantic-release/semantic-release) to automate releases. Every commit is run on [Circle CI](https://circleci.com/gh/bifravst/cat-tracker-fw/tree/saga) and depending on the commit message an new GitHub [release](https://github.com/bifravst/cat-tracker-fw/releases) is created and the pre-build hex-file is attached.
