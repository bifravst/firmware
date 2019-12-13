# Cat Tracker Firmware

[![GitHub Actions](https://github.com/bifravst/cat-tracker-fw/workflows/Build%20and%20Release/badge.svg)](https://github.com/bifravst/cat-tracker-fw/actions)
[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)

Read the documentation at https://bifravst.github.io/.

This application is built with [fw-nrfconnect-nrf](https://github.com/NordicPlayground/fw-nrfconnect-nrf).

Supported boards:

- Asset Tracker (PCA10015) (`west build -b nrf9160_pca10015ns`)
- Thingy:91 (PCA20035) (`west build -b nrf9160_pca20035ns`)
- nRF9160 DK (PCA10090) (`west build -b nrf9160_pca10090ns`)

## Building

In order to build the `cat_tracker` appliction, the AWS IoT Core broker hostname
**must** to be configured:

```
    echo "CONFIG_AWS_IOT_BROKER_HOST_NAME=\"<your broker hostname>\"" >> applications/cat_tracker/prj.conf
```

### Setting the applicaton version

You **may** configure the application version that is sent as part of the device
information.

```
    echo "CONFIG_CAT_TRACKER_APP_VERSION=\"<your version string>\"" >> applications/cat_tracker/prj.conf
```

## Automated releases

Every commit is run using [GitHub Actions](https://github.com/features/actions) and a new GitHub [release](https://github.com/bifravst/cat-tracker-fw/releases) is created and pre-build hex-files for all supported boards are attached.

See https://github.com/bifravst/cat-tracker-fw/issues/28
