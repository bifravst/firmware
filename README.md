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

## Functional Overview

The cat tracker device behaviour is dictated by a sequence of repeating function calls called from the main module module after initialization. This repeating sequence of functions are affected by dynamic device configurations that is manipulated via the Cat Tracker Web Application. In the firmware these configurations takes form of global variables local to the main module which are checked by the respective functions in the repeating sequence.

The device publishes data to a desired compatible cloud service sequentially and upon movement depending on the current device mode. The device mode can either be "active" or "passive". In "active" mode the device publishes data sequentially every "active wait time" interval and in "passive" mode the device publishes data every "passive wait time" as long as there is movement detected by the external accelerometer.

### Repeating Sequence

 1. Check device mode. Sleep and wait for movement if in "passive" mode.
 2. Search for GPS fix.
 3. Check current cellular network registration status.
 4. Publish data according to the schema below if device is still registered to the cellular network.
 5. Sleep
 6. Repeat

#### 1. Check device mode:

The application checks the current device mode. If it is in "active mode" the device will immediately try to get a gps fix. If it is in "passive" mode the main thread will go to sleep and not continue until movement over a configurable threshold value is detected.

#### 2. Search for GPS fix

The device will try to obtain a GPS position fix within the GPS timeout duration. If the fix is not obtained within the time limit the publication message in the next cloud publication will not contain GPS data. If a fix is obtained within the time limit the publication message will contain GPS data. In addition, the time obtained from the GPS fix is used to refresh the internal UTC date time timestamp variable used to calculate timestamped attached to every publication message.

#### 3. Check current cellular network registartion status

Check if the device is still registered to the cellular network. This check manipulates an internal semaphore that will block cloud publication if the device is not registered to a cellular network, avoiding unnessecary call to functionality if the device is infact not connected. However, the modem can believe that it is registered to a network when infact it is not. This is due to the use of PSM instervals which enables the connection intervals between the device and the connected cellular tower to occur less frequence. This can dramatically decrease the resolution of which the modem updates it cellular network registration state. This is not a problem, the publication functionality is non blocking and the application will not hault if it tries to publish without a connection.

#### 4. Publish data

Publish data to the cloud

#### 5. Sleep

The main thread sleeps in the duration of the current device mode wait time.

| Mode    | GPS fix? | Sensor/Modem data published        |
|---------|----------|------------------------------------|
| Active  | Fix      | Battery, modem, GPS                |
| Active  | No fix   | Battery, modem                     |
| Passive | Fix      | Battery, modem, GPS, accelerometer |
| Passive | No fix   | Battery, modem, accelerometer      |

### Cloud Communication

 * Upon a cloud connection the device will fetch its desired configuration from the cloud, apply the configuration and report the configuration back to the cloud.
 * Every publication to cloud the device will recieve a message from the cloud if a setting has been changed in the web application. The device will then apply the updated configuration(s) and report its new configurations
   back to the cloud. Publication of sensor/modem/GPS data and updating device configurations happens in a determined order listed below.

#### Successful connection to cloud

```sequence
Cat tracker->Cloud service: Requesting current configuration
Cloud service->Cat tracker: Send configuration
Cat tracker->Cloud service: Acknowledge new configuration
Cat tracker->Cloud service: Publish modem data (device information, network information)
```

#### Data publication

```sequence
Cat tracker->Cloud service: Publish Battery, acceleromter and GPS data
Cloud service->Cat tracker: Send new configuration(if any)
Cat tracker->Cloud service: Acknowledge new configuration
Cat tracker->Cloud service: Publish modem data (device information, network information)
Cat tracker->Cloud service: Publish buffered GPS data
```

### Timestamping

All the data published to the cloud are timestamped in sample time UTC. For more information about how timestamping is carried out in the cat tracker firmware see section [Timestamping](https://bifravst.gitbook.io/bifravst/v/saga/cat-tracker-firmware/protocol#timestamping) the GitHub handbook.

### Default configurations

If the device is unable to connect to the cloud after boot it will used the following configurations:

| Configuration                           |        Default |
|-----------------------------------------|----------------|
| Active wait time                        |         60 sec |
| Movement resolution (passive wait time) |         60 sec |
| Movement timeout                        |       3600 sec |
| Device mode                             |         Active |
| GPS timeout                             |         60 sec |
| Accelerometer threshold                 | 100/10 (m/s^2) |
