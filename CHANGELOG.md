Release for June 30, 2016

# C Device SDK
- Updated the default option on build scripts for uploadToBlob to ON for all platforms
- Updates Azure IoT C SDKs to use a new runtime switchable logging
- Updates Azure IoT C SDKs to use consolelogger_log as default logger
- Update IoTHubClient_SendComplete to use IOTHUB_CLIENT_CONFIRMATION_RESULT callback
- Added initial changes to support using x509 certificates on C client + added HTTP sample
- Updated the E2E tests to use the new C SDK Service Client
- Extended Upload to Blob to support files larger than 64MB.

# JavaWrapper SDK
- Added support for blob upload to JavaWrapper device client

# C# Device SDK
- Added support for x509 certificates on C# device client MQTT transport

# C# Service SDK
- C# service SDK now has support for UWP (Courtesy of Artur Laksberg)

# NodeJs Device SDK
- Fixed sample code in readme documentation (GitHub issue #671)
- Added support for using x509 certificates over AMQP and AMQP over websockets

# NodeJs Service SDK
- Added support for using x509 certificates

# Python
- Added support for blob upload to Python device client

# General
- Updated correct path of samples for C SDK on Linux Get-Started documentation
- Removed usage of LogInfo/LogError in C E2E tests
- Consolidated definition of DEFINE_ENUM_STRING into iothub_client_ll.c, removing definition from samples
- Updated DeviceExplorer to Auto scroll events text box
- Several documentation updates for Azure IoT certification, Get-Started guides

# Bug fixes
- Fixed general build bugs for mbed, Windows and Linux platforms
- Fixed linux build.sh warning regarding unary operator
- Fixed C SDK samples to send disposition on exit
- Updated C SDK AMQP over Websockets transport interface to include recent API changes, plus general bug fixes
- Fixed MBED build scripts to delete existing files from MBED.org repos before updating with new files (fix for deleted fixes not being updated online)
- Fixed general bugs in Python build scripts on Windows



# Node.js Device SDK
- Add support for x509 authentication with HTTP
- Node.js v6 support
- Migrate to node-amqp10 v3.x.x

# Node.js Service SDK
- Add support for x509 certificate thumbprints in the Registry
- Migrate to node-amqp10 v3.x.x

Release for June 3, 2016
# C Device SDK
- Upload to blob functionality

# C# Device SDK
- Add new UploadToBlobAsync API implementing upload of a stream to Azure Storage.
- Now utilizes amqp 1.1.5

# C# Service SDK
- Now utilizes amqp 1.1.5
- Adding API support for receiving file upload notifications

#Java SDK
- Implemented message timeout for AMQP protocol
- Java device client maven package is now Java 7/Android compatible
- Java sample for Android added

# Internal Changes
- Updated the shared utility to use the ctest repo
- Shared utility updated with a new string function

# Node.js Device SDK
- Upload to blob

# Node.js Service SDK
- Upload notifications
- Deprecating `Amqp.getReceiver` in favor of `Amqp.getFeedbackReceiver`. `Amqp.getReceiver` will be removed for the next major release

# General
- Change the location of the binaries produced with the build script to be contained within the repository directory
- Added arm support for the Nuget and apt-get packages

# Bug fixes
- Adjusted the tls not to provide credentials by default.
- Fixed regressions with AMQP on UWP
- Python vcproj file contained a false ref to azure_shared_utility 1.0.7


Release for May 20, 2016

# C# Device SDK
- Device Client code cleanup & refactoring (changed to pipeline model)
- Thread pool for MQTT threads
- More sophisticated error handling
- Retry operation if transient error occurred
- Support for X.509 client certificates as an authentication mechanism for devices

# General
- Iothub-explorer - Pull request #551, tomconte – Add an option to generate a SAS Token for a device

# Bug fixes
- Fix tlsio_schannel bug (not all input bytes were being read)
- Fix for the linux socket io layer
- C# MQTT Fixed exception handling to properly propagate to a user
- C# MQTT Fixed race in the message complete operation
- Linux defaults to not run the e2e tests

# Internal Changes
- Fixed C longhaul tests for Windows, Linux and MBED platforms


Release for May 6, 2016

# C Device SDK
- Cloud-to-Device message application properties exposed by AMQP transport to IoT client layer.
- Parse for a device SAS token input part of the device connection string.
- Added support for AMQP on Arduino Yun builds

# Java SDK
- Device SDK is now Java 7 compatible. It can be used to target Android projects 4.0.3 or higher with HTTPS and MQTT protocols.

# C# Service SDK
- Support for PurgeMessageQueueAsync
- Revised service API version with added support for custom blob names for Import/ExportDevicesAsync + quality of life changes for bulk operations
- Adding support for receiving file upload notifications from service client. 
	- Implemented 'AmqpFileNotificationReceiver'
	- Moved common code for 'AmqpFileNotificationReceiver' and 'AmqpFeedbackReceiver' to common helpers


# Node Device Client
- Added a node-RED module for Azure IoT Hub

# Python SDK
- Bug fixes for messageTimeout option and message properties in the Python binding
- Build improvements

# General
- Documentation fixes
- Allow iothub-explorer users to generate device SAS tokens with 'sas-token' command

# Bug fixes

- GitHub issue #454: message-id and correlation-id properties now are exposed when using AMQP transport on IoT Hub C Device Client;
- Java Device SDK - fixed a bug which was preventing message properties being sent over AMQP protocol.

# Internal Changes
- Add a CHANGELOG file to track changes that are going into the upcoming release;