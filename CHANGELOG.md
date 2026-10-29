# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [semantic versioning](https://semver.org/). The API may change at any time during `0.x`.

## [Unreleased]

### Added
- Test-then-save: the network the Owner enters in the Provisioning Portal is tried while the access point stays up and saved only if it connects, or straight away with "Save anyway". The page shows the progress, the reason for a failure and a warning that the phone may drop off for a moment, and keeps asking for the result until the phone is back. `onProvisioningCompleted` reports the saved network and the portal closes 30 s later (#15).
- Network list: the Provisioning Portal's page lists the networks in range from a non-blocking scan, each SSID once at its strongest, hidden and weak networks (below -90 dBm, `esp.portal().setMinSignal()`) left out, plus "Other network…" for a hidden one. `esp.startScan()`, `esp.scanComplete()` and `esp.scanResults()` give sketches the same list (#14).
- Captive portal: while the Provisioning Portal is open, a DNS server answers every name with the access point's address and every other address redirects to the setup page, so phones open it by themselves; `esp.portal().setCaptivePortal(false)` turns it off (#13).
- Provisioning Portal: opens by itself without a Saved Network, or with `esp.startPortal()`; `esp.stopPortal()`, `esp.portalState()` and the settings on `esp.portal()` (SSID, WPA2 password, inactivity timeout, reopening, opening after failing to connect). A scan first puts the access point on the channel of the strongest Saved or visible network, and the pages answer only on the access point. Scans are shared by the connection and the portal (#12).
- Web page pipeline: page sources in `src/web/`, their text in `src/web/strings.en.json`, and `scripts/build-web.py`, which inlines and gzips each page into a committed header so it's served straight from flash (#11).
- Library skeleton: PlatformIO and Arduino library layout, `EspManager` class with `begin()` and `process()` (no behaviour yet), `examples/Basic` and host unit tests.
- Hardware interfaces `WifiDriver`, `Storage`, `Clock` and `HttpServer`, with ESP32 implementations (Wi-Fi, NVS, `esp_timer`, `WebServer`/`DNSServer`) and fakes for the host tests. `EspManager` can be given other implementations for testing (#3).
- `scripts/check-includes.sh`, which keeps Arduino and ESP-IDF headers inside `src/platform/`.
- Internal logging with the levels `Error`, `Warn`, `Info`, `Debug` and `Verbose`, off by default; `esp.setLogLevel()` turns it on and prints to `Serial` (#4).
- `SavedNetwork` (SSID, password, priority, DHCP or static IP) with validation, and `SavedNetworkStore`, which keeps the list as one versioned record in NVS. A damaged record, or one from newer firmware, reads as no Saved Networks and is left in place (#6).
- Hostname: defaults to `esp-<last 6 hex digits of the MAC>`; `esp.setHostname()` and `esp.setNamePrefix()` change it, `esp.hostname()` returns it. DHCP by default, static addressing per Saved Network (#10).
- `esp.waitForConnection(timeoutMs)`: an opt-in wait for the connection that keeps calling `process()` (#9).
- Reconnecting with exponential backoff (1 s up to 5 min, random variation, `esp.setRetryDelays()`); each attempt scans and joins the best reachable Saved Network; `esp.lastFailure()` and `espmgr::toString(FailureReason)` (#8).
- Connecting: `begin()` loads the Saved Network and `process()` connects to it, retries after a failed attempt (20 s timeout) and reconnects after a lost connection. `esp.state()` returns `Idle`, `Connecting`, `Connected` or `Waiting`; `esp.networks()` saves, lists and clears the Saved Network (one for now) (#7).
- Events: `onConnected`, `onDisconnected`, `onConnectFailed`, `onConnectionStateChanged`, `onPortalStarted`, `onPortalStopped` and `onProvisioningCompleted`, each taking any number of listeners, which are only called from inside `process()` (#5).
- On-device tests of the ESP32 hardware code (`test/test_hw_platform/`), with `scripts/build-hw-tests.sh` to build them and `scripts/run-hw-tests.py` to flash, run and report them (#28).
