# Cat Tracker Firmware

[![GitHub Actions](https://github.com/bifravst/cat-tracker-fw/workflows/Build%20and%20Release/badge.svg)](https://github.com/bifravst/cat-tracker-fw/actions)
[![semantic-release](https://img.shields.io/badge/%20%20%F0%9F%93%A6%F0%9F%9A%80-semantic--release-e10079.svg)](https://github.com/semantic-release/semantic-release)
[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)

Read the documentation at https://bifravst.github.io/.

This application is built with [fw-nrfconnect-nrf](https://github.com/NordicPlayground/fw-nrfconnect-nrf).

Supported boards:

- Asset Tracker (PCA10015) (`west build -b nrf9160_pca10015ns`)
- Thingy:91 (PCA20035) (`west build -b nrf9160_pca20035ns`)
- nRF9160 DK (PCA10090) (`west build -b nrf9160_pca10090ns`)

## Automated releases

This project uses [Semantic Release](https://github.com/semantic-release/semantic-release) to automate releases. Every commit is run using [GitHub Actions](https://github.com/features/actions) and depending on the commit message an new GitHub [release](https://github.com/bifravst/cat-tracker-fw/releases) is created and pre-build hex-files for all supported boards are attached.
