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

## Licence

MIT, see [LICENSE](LICENSE).
