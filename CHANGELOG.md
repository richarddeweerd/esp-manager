# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [semantic versioning](https://semver.org/). The API may change at any time during `0.x`.

## [Unreleased]

## [0.2.0] - 2026-11-10

Settings for the Owner to fill in, and diagnostics.

### Added
- Settings (EP06): `esp.settings()` declares text, password, number, checkbox and select Settings, in Sections, each with validation (`.required()`, `.maxLength()`, `.range()`, `.option()`, `.validate()`). Values are stored in NVS only when they differ from the default. `esp.onSettingChanged()` reports changes, `esp.settings().complete()` says whether required ones are filled in, and `esp.settingsReset()` now returns them to their defaults. The Provisioning Portal shows them during first setup, one screen per Section, and from its list of Saved Networks afterwards, through `GET /api/settings`, `POST /api/settings` and `POST /api/settings/check`. Settings marked `.editableInManagement()` appear in a *Settings* tab in the Management Interface, whose Device tab also has a Settings Reset, `POST /api/settings-reset`. A new example, `Settings`, and a guide in the README (#29, #30, #31, #32).
- Log handler: `esp.setLogHandler(fn)` sends the library's log lines to the sketch instead of `Serial`, as an `espmgr::LogEntry` with the level, time, tag, message and finished line; `nullptr` goes back to `Serial`. It doesn't change the level, which `esp.setLogLevel()` still sets (#33).
- Device status: the DNS server and why the device last started (power-on, reset button or USB, software restart, crash, watchdog, brownout or deep sleep), in `esp.status()` (`dns`, `resetReason`) and on the status pages (#34).
- Connection History: `esp.connectionHistory().entries()` lists the most recent connection events, oldest first: started, connected (SSID, signal, channel), connect failed (SSID, reason) and disconnected (reason), each with the time since start-up. 20 are kept by default, `setCapacity()` before `begin()` changes it (0 to 50). It's kept in RAM, so a restart clears it (#35).
- The Management Interface's Status tab lists the Connection History as *Recent events*, newest first, from `GET /api/history`. The Provisioning Portal doesn't serve it (#36).
- A new example, `Diagnostics`, and a guide to the log handler, the status and the Connection History in the README (#37).

## [0.1.0] - 2026-11-01

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

[Unreleased]: https://github.com/richarddeweerd/esp-manager/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/richarddeweerd/esp-manager/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/richarddeweerd/esp-manager/releases/tag/v0.1.0
