# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [semantic versioning](https://semver.org/). The API may change at any time during `0.x`.

## [Unreleased]

## [0.1.0] - 2026-10-31

The first release: connecting, setup from a phone and an optional management page, for ESP32, ESP32-S3 and ESP32-C3 on Arduino-ESP32 core 3.x.

### Added
- `EspManager` with `begin()`, which returns at once, and `process()`, which does all the work from `loop()` without blocking.
- Connecting: `process()` connects to the Saved Networks and reconnects after a lost connection. Each attempt scans and joins the first Saved Network in range (or tries the first one, in case it's hidden), and gives up after 20 s. `esp.state()` is `Idle`, `Connecting`, `Connected` or `Waiting`; `esp.lastFailure()` and `espmgr::toString(FailureReason)` say why the last attempt failed (#7, #8).
- Retrying with exponential backoff, 1 s up to 5 min with random variation; `esp.setRetryDelays()` changes the limits (#8).
- `esp.waitForConnection(timeoutMs)`: an opt-in wait that keeps calling `process()` (#9).
- Saved Networks in NVS through `esp.networks()`: up to 5 by default (`setMaxNetworks()`, 1 to 20), tried in the Owner's order. `save()` puts a new SSID on top and updates a known one in its place, and refuses a new one when the list is full; `remove()`, `move()`, `count()`, `contains()`, `full()` and `list()`. DHCP, or a static address per network. Networks are validated before they're saved (#6, #7, #18, #21).
- Hostname `esp-<last 6 hex digits of the MAC>` by default; `esp.setHostname()`, `esp.setNamePrefix()` and `esp.hostname()` (#10).
- Provisioning Portal: an access point with a setup page, opened by itself when there's no Saved Network, or with `esp.startPortal()`; `esp.stopPortal()` and `esp.portalState()`. The page lists the networks in range from a scan, offers "Other network…" for a hidden one, and tests a network before saving it, with "Save anyway" for one that's out of range. It warns that the phone may drop off for a moment while the device tests, and picks up the result when the phone is back. With Saved Networks it opens on their list, where the Owner reorders or removes them. Settings on `esp.portal()`: SSID, WPA2 password, inactivity timeout, reopening, opening after failing to connect, the captive portal and the minimum signal (#11–#15, #21).
- Captive portal: phones that join the access point open the setup page by themselves (#13).
- Management Interface, off by default: `esp.management().enable()` turns on a page on the device's address in the Owner's network, behind HTTP Digest authentication, with Wi-Fi (move, remove and add Saved Networks), Status and Device (restart and resets) tabs. The Provisioning Portal asks the Owner for its password; only a hash is stored. `setUser()`, `setRealm()` and `setPassword()` (#17, #19, #21).
- Device status: `esp.status()` and `esp.setFirmwareVersion()`, shown in the Provisioning Portal and the Management Interface (#16).
- Resets and restart: `esp.networkReset()`, `esp.settingsReset()` (nothing to reset until Settings exist), `esp.factoryReset()` and `esp.restart()` (#19).
- Scanning for sketches: `esp.startScan()`, `esp.scanComplete()` and `esp.scanResults()`, with the same cleaned-up list as the setup page (#14).
- Events: `onConnected`, `onDisconnected`, `onConnectFailed`, `onConnectionStateChanged`, `onPortalStarted`, `onPortalStopped` and `onProvisioningCompleted`, each taking any number of listeners, which are only called from inside `process()` (#5).
- Logging to `Serial`, off by default: `esp.setLogLevel()` with `Error`, `Warn`, `Info`, `Debug` or `Verbose`. Passwords are never logged (#4).
- Examples: `Basic`, and `Button`, which opens the Provisioning Portal on a short press of the BOOT button and starts a Network Reset or Factory Reset when it's held (#20).
- Hardware interfaces (`WifiDriver`, `Storage`, `Clock`, `HttpServer`, `LogSink`, `System`) with ESP32 implementations, fakes and host unit tests, plus on-device tests and the scripts to build and run them (#3, #28).

[Unreleased]: https://github.com/richarddeweerd/esp-manager/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/richarddeweerd/esp-manager/releases/tag/v0.1.0
