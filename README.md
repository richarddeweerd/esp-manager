# EspManager

EspManager is an Arduino library for ESP32 devices. It gets a device onto Wi-Fi and lets people set it up, manage it and keep it up to date without reflashing over USB.

> **Status: early development.** There is no usable code yet. The API shown below is the intended design and will change before `1.0.0`.

## Planned features

**First release (`0.1`):**
- Connects automatically to saved Wi-Fi networks, with non-blocking reconnecting and backoff.
- Setup through a temporary access point with captive-portal pages: pick a network from a scan, or enter a hidden one. The network is tested before it's saved.
- Multiple saved networks with priorities, and DHCP or static IP for each network.
- An optional password-protected management page on the local network, with status, restart and resets.
- Events for connection and setup changes, always called from your `loop()`.
- Never blocks your `loop()`: all work happens in `process()`.

**Later:**
- Custom settings fields for your own firmware (device name, MQTT server, …).
- Diagnostics and logging, with your own log callback.
- Theming and branding of the web pages.
- Over-the-air firmware updates from your own server, with signature checks and rollback.
- Optional NTP time sync with time zones.

## Intended usage

```cpp
#include <EspManager.h>

EspManager esp;

void setup()
{
    esp.begin();    // connect to a saved network, or start setup mode
}

void loop()
{
    esp.process();  // never blocks
    // your own code
}
```

The library is silent by default. To see what it does, start `Serial` and pick a log level (`Error`, `Warn`, `Info`, `Debug` or `Verbose`):

```cpp
Serial.begin(115200);
esp.setLogLevel(espmgr::LogLevel::Debug);
```

Passwords are never logged at any level.

To react to what the library does, add listeners. Any number can listen to each event, and they're only ever called from inside `process()`, so they may safely call back into `esp`:

```cpp
esp.onConnected([](const espmgr::ConnectedInfo& info) {
       Serial.printf("Connected to %s as %s\n", info.ssid.c_str(), info.ip.toString().c_str());
   })
   .onPortalStarted([](const std::string& ssid) { Serial.printf("Set me up via %s\n", ssid.c_str()); });
```

The events are `onConnected`, `onDisconnected`, `onConnectFailed`, `onConnectionStateChanged`, `onPortalStarted`, `onPortalStopped` and `onProvisioningCompleted`.

## Supported hardware

- ESP32, ESP32-S3 and ESP32-C3 on Arduino-ESP32 core 3.x, built with PlatformIO.
- ESP8266 is not supported.

## Security notes

- Wi-Fi credentials are stored in the ESP32's NVS flash. They are only encrypted at rest if you enable ESP32 flash and NVS encryption.
- The device can't realistically serve HTTPS, so the management page uses HTTP Digest authentication and assumes a trusted local network.

## Building and testing

Requires [PlatformIO](https://platformio.org/) running on Python 3.10–3.14.

```bash
pio run
```

builds `examples/Basic` for ESP32, ESP32-S3 and ESP32-C3.

```bash
pio test -e native
```

runs the unit tests on your computer; no board is needed.

The checks that run on every merge request are in `scripts/`:

```bash
scripts/build-examples.sh esp32dev
```

builds every sketch in `examples/` for one board. `scripts/check-format.sh` checks formatting against `.clang-format`, `scripts/check-versions.py` checks that `library.json`, `library.properties` and `src/EspManagerVersion.h` have the same version, and `scripts/check-includes.sh` checks that only `src/platform/` uses Arduino or ESP-IDF headers.

The library's logic reaches the hardware only through four small interfaces in `src/core/` (`WifiDriver`, `Storage`, `Clock` and `HttpServer`). The ESP32 versions are in `src/platform/esp32/`; the unit tests use the fakes in `test/fakes/` instead, which is why they run without a board.

### On-device tests

The tests in `test/test_hw_platform/` run on a board and check the ESP32 versions of those interfaces against real hardware: scanning and connecting, the access point, NVS storage and the web server. Build them and run them on a connected board with

```bash
scripts/build-hw-tests.sh esp32-s3-devkitc-1 hw-firmware
ESPMGR_TEST_SSID=<network> ESPMGR_TEST_PASSWORD=<password> \
    scripts/run-hw-tests.py --port <serial port> --firmware hw-firmware/esp32-s3-devkitc-1/test_hw_platform.bin
```

`run-hw-tests.py` needs `esptool` and `pyserial`. It flashes the board, sends it the Wi-Fi network to test against over the serial port (the password is never printed), checks the board's web server from your computer, so both must be on the same network, and can write a JUnit report with `--junit`. Without a network the tests that need one are ignored. Plain `pio test -e <board>` runs them too, also without the network tests. The ESP32-S3 environment prints to the native USB port.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), including the review checklist.

## Licence

MIT, see [LICENSE](LICENSE).
