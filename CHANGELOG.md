# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [semantic versioning](https://semver.org/). The API may change at any time during `0.x`.

## [Unreleased]

### Added
- Library skeleton: PlatformIO and Arduino library layout, `EspManager` class with `begin()` and `process()` (no behaviour yet), `examples/Basic` and host unit tests.
- Hardware interfaces `WifiDriver`, `Storage`, `Clock` and `HttpServer`, with ESP32 implementations (Wi-Fi, NVS, `esp_timer`, `WebServer`/`DNSServer`) and fakes for the host tests. `EspManager` can be given other implementations for testing (#3).
- `scripts/check-includes.sh`, which keeps Arduino and ESP-IDF headers inside `src/platform/`.
