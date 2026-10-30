# EspManager

EspManager is an Arduino library for ESP32 devices. It gets a device onto Wi-Fi and lets people set it up, manage it and keep it up to date without reflashing over USB.

> **Status: early development.** Connecting and setup through the Provisioning Portal work; the rest is still being built. The API will change before `1.0.0`.

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

On a new device the Owner sets up Wi-Fi from a phone: join the device's access point, pick the network on the page that opens, enter its password. The device tests the network before it saves it; if the test fails, the page says why and offers to try again or to save it anyway (for a network that's out of range right now). `onProvisioningCompleted` reports the saved network, and the portal closes 30 seconds later. Testing runs the access point and the station connection at the same time, so the access point may move to the router's channel and drop the phone for a moment; the page warns about this and picks up the result once the phone is back.

After a restart the device connects to its Saved Networks by itself. It keeps up to five, in the Owner's order: it joins the first one on the list that's in range. A network the Owner adds goes to the top. Both web sites list the Saved Networks, and the Owner can move them up and down or remove them there. A sketch can also change the list directly, and check where the connection stands:

```cpp
esp.networks().setMaxNetworks(3);              // how many fit (1 to 20, 5 by default)
esp.networks().save("Home", "wifi password");  // stored in NVS, on top of the list
esp.networks().move("Home", 1);                // second place; 0 is the top
esp.networks().remove("Old router");
if (esp.state() == espmgr::ConnectionState::Connected) { /* … */ }
```

`save()` returns `false` when the list is full, unless that SSID is saved already: then it's updated in its place. A list stored by firmware with a higher limit is kept as it is, but nothing can be added until it's below the new limit. Removing the network the device is connected to doesn't disconnect it; the device moves on when that connection ends.

The device tells the router its name, `esp-` plus the last six hex digits of its MAC address (such as `esp-abcdef`). Change it before `begin()` with `esp.setHostname("kitchen-lamp")`, or keep the MAC part and change only the prefix with `esp.setNamePrefix("lamp")`. A Saved Network uses DHCP unless it's saved with a static address:

```cpp
espmgr::SavedNetwork office;
office.ssid = "Office";
office.password = "wifi password";
office.dhcp = false;
office.staticIp = {espmgr::Ipv4Address(192, 168, 1, 50), espmgr::Ipv4Address(192, 168, 1, 1),
                   espmgr::Ipv4Address(255, 255, 255, 0), espmgr::Ipv4Address(192, 168, 1, 1), {}};
esp.networks().save(office);
```

`begin()` never waits. A sketch that needs Wi-Fi before it goes on can opt in to waiting with `esp.waitForConnection(10000)`, which keeps calling `process()` and returns `true` once connected or `false` after the timeout (10 s here); connecting carries on in `process()` either way.

The connection state is `Idle` (no Saved Network), `Connecting`, `Connected` or `Waiting` (to try again after a failure or a lost connection). Each attempt scans first and joins the first Saved Network in range; if none is in range, it tries the first on the list anyway, in case it's hidden. After a failure the device waits before trying again, 1 s at first and twice as long after each further failure, up to 5 minutes, with some randomness so devices don't all retry at once. `esp.setRetryDelays(minMs, maxMs)` changes those limits, and `esp.lastFailure()` tells why the last attempt failed (`espmgr::toString()` turns it into text such as "wrong password").

Without a Saved Network, the device opens the Provisioning Portal: an access point named after the device (`esp-abcdef`), open by default. `esp.startPortal()` opens it at any other time, for example from your own button code, and `esp.stopPortal()` closes it; `esp.portalState()` is `Off` or `Active`. The pages only answer on that access point, never on the network the device is connected to. The setup page lists the networks in range, strongest first, each SSID once, without hidden networks or ones weaker than -90 dBm (`esp.portal().setMinSignal(-80)` changes that), plus "Other network…" for a hidden one. It's a captive portal: phones that join the access point show "sign in to network" and open the setup page by themselves (`esp.portal().setCaptivePortal(false)` turns that off; the page is then at `http://192.168.4.1/`). It closes after 10 minutes without a request, and opens again while there's still no Saved Network. The settings are on `esp.portal()`:

```cpp
esp.portal().setPassword("setup-password");   // WPA2 instead of open; 8 to 63 characters
esp.portal().setSsid("Lamp setup");           // instead of the device name
esp.portal().setTimeout(5 * 60 * 1000);       // 0 keeps it open
esp.portal().setRestartOnTimeout(false);      // stay closed after a timeout
esp.portal().setOpenAfterFailingFor(15 * 60 * 1000);  // also open when not connected for 15 min
```

`esp.status()` returns everything the status pages show: connection state, SSID, IP address, gateway, signal, channel, hostname, MAC, uptime, free heap, chip and versions. Set your own version for it with `esp.setFirmwareVersion("1.4.2")`. The Provisioning Portal's page shows the same under "Status"; it never shows a password.

The Management Interface is a password-protected page on the device's own address on the Owner's network, with a Wi-Fi, a Status and a Device tab. It's off by default; turn it on before `begin()`:

```cpp
esp.management().enable();
esp.management().setUser("owner");           // "admin" by default
esp.management().setRealm("Kitchen lamp");   // what the browser's login shows; the hostname by default
esp.begin();
```

The Provisioning Portal then asks the Owner for the management password, which is stored when setup completes. Until a password is stored, the Management Interface stays off. If the Owner enters the password some other way, over serial or Bluetooth for example, store it with `esp.management().setPassword(password)`. Never hard-code a password in the firmware: every device would share it. Only the HTTP Digest hash of the password is stored, and it includes the user and realm, so changing either means the Owner has to set the password again through the Provisioning Portal.

Three resets, for your own buttons or menus, and a restart:

```cpp
esp.networkReset();    // forget every Saved Network and leave; the Provisioning Portal opens again
esp.settingsReset();   // Settings back to their defaults (no Settings yet, so nothing for now)
esp.factoryReset();    // erase everything stored, your own Preferences included, and restart
esp.restart();         // restart a moment later, from process()
```

On the Management Interface's Wi-Fi tab the Owner can also add a network. It's saved without a test, since testing would drop the Owner's own connection; the Provisioning Portal is the place to test one. The last Saved Network can't be removed there; that's what the Network Reset is for.

The Management Interface's Device tab offers the restart, the Network Reset and the Factory Reset, each after a confirmation. The library reads no buttons itself; [examples/Button](examples/Button) shows how to wire one up.

Your own code can scan too, without blocking: `esp.startScan()`, then once `esp.scanComplete()` is true, `esp.scanResults()` gives the same cleaned-up list as the setup page (SSID, signal in dBm, channel and whether it's secured).

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
- The device can't realistically serve HTTPS, so the Management Interface uses HTTP Digest authentication and assumes a trusted local network. Digest keeps the password off the network, but anyone on it can read the pages themselves. Only the Digest hash of the password is stored, never the password.

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

The web pages are written as plain HTML, CSS and JavaScript in `src/web/<page>/`, with all their text in `src/web/strings.en.json`. `scripts/build-web.py` turns each page into one gzipped file inside a generated header (`src/web/generated/`), which is committed, so building the library needs no extra tools. Run it after changing a page; `scripts/build-web.py --check` fails if a header is out of date.

The library's logic reaches the hardware only through small interfaces in `src/core/` (`WifiDriver`, `Storage`, `Clock`, `HttpServer`, `LogSink` and `System`). The ESP32 versions are in `src/platform/esp32/`; the unit tests use the fakes in `test/fakes/` instead, which is why they run without a board.

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
