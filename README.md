# Cat Tracker Firmware

[![GitHub Actions](https://github.com/bifravst/cat-tracker-fw/workflows/Build%20and%20Release/badge.svg)](https://github.com/bifravst/cat-tracker-fw/actions)
[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)

This application is built with [fw-nrfconnect-nrf](https://github.com/NordicPlayground/fw-nrfconnect-nrf).

## Building

Follow the instructions [in the handbook](https://bifravst.gitbook.io/bifravst/v/saga/cat-tracker-firmware/compiling)

## Automated releases

> **Note:** These pre-build binaries cannot be used with *your* board, because they are hard-coded to a specific MQTT broker hostname. [We plan to provide pre-build binaries which can be configured to use any MQTT broker.](https://github.com/bifravst/cat-tracker-fw/issues/29)

Every commit is run using [GitHub Actions](https://github.com/features/actions) and a new GitHub [release](https://github.com/bifravst/cat-tracker-fw/releases) is created and pre-build hex-files for all supported boards are attached.

See https://github.com/bifravst/cat-tracker-fw/issues/28
