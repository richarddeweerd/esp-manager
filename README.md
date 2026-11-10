# EspManager

EspManager is an Arduino library for ESP32 boards. It gets a device onto Wi-Fi, lets its Owner set it up from a phone, and keeps it connected, without ever blocking your `loop()`.

```cpp
#include <Arduino.h>  // needed in a .cpp file; an .ino gets it by itself
#include <EspManager.h>

EspManager esp;

void setup()
{
    esp.begin();    // returns at once
}

void loop()
{
    esp.process();  // does the work in small steps; never blocks
    // your own code
}
```

That's the whole [Basic example](examples/Basic). On a new board it opens a Wi-Fi access point with a setup page; once the Owner has picked a network there, the device joins it, and after every restart it reconnects by itself.

Version `0.3.0`. The API may still change before `1.0.0`; see [CHANGELOG.md](CHANGELOG.md).

- [What it does](#what-it-does)
- [Terms](#terms)
- [Quick start](#quick-start)
- [Guide](#guide)
- [Examples](#examples)
- [Supported hardware](#supported-hardware)
- [Security notes](#security-notes)
- [Development](#development)

## What it does

- Connects to the Saved Networks by itself, in the Owner's order, and reconnects after a lost connection, waiting longer after each failure.
- Setup from a phone through a temporary access point and captive portal: pick a network from a scan or enter a hidden one. The device tests the network before it saves it.
- Up to 5 Saved Networks by default, each with DHCP or a static address.
- Your own settings on the web pages: text, passwords, numbers, checkboxes and selections, checked and stored in NVS for you.
- An optional, password-protected management page on the Owner's own network: the Saved Networks, the device's status, chosen settings, a restart and the resets.
- Events for connection and setup changes, always called from inside `process()`.
- Diagnostics: the library's log lines in your own handler, a Connection History of recent connects, failures and drops, and why the device last started.
- Your product's look on both web pages: a title, a logo, a light or dark theme and your own CSS.
- Everything happens in `process()`; nothing waits unless you ask for it.

Planned for later versions: over-the-air updates and NTP time.

## Terms

The README uses a few words in a fixed sense:

- **Developer**: you, writing the sketch that uses the library.
- **Owner**: the person who has the device and sets it up, usually from a phone. They never see your code.
- **Saved Network**: a Wi-Fi network the device has stored, with its password, to connect to.
- **Provisioning Portal**: the setup site. The device opens its own access point and serves a setup page on it, so the Owner can choose a Saved Network. It only answers on that access point.
- **Management Interface**: the management site, off unless you turn it on. A password-protected page on the device's address in the Owner's own network, once the device is connected.
- **Setting**: a named value your firmware declares and the Owner fills in on the web pages, such as a server's name. Settings are grouped in **Sections**.
- **Connection History**: the device's record of its recent connection events, kept in memory until it restarts.
- **Branding**: your title, logo, theme and CSS for the web pages.

## Quick start

### 1. Install

With [PlatformIO](https://platformio.org/), add the library from the [PlatformIO Registry](https://registry.platformio.org/libraries/richarddeweerd/EspManager) and the pioarduino platform to your `platformio.ini`. EspManager needs Arduino-ESP32 core 3.x, which PlatformIO's own `espressif32` platform doesn't ship; [pioarduino](https://github.com/pioarduino/platform-espressif32) does. This is the release the library is built and tested with:

```ini
[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip
framework = arduino
board = esp32dev
monitor_speed = 115200
lib_deps = richarddeweerd/EspManager@^0.3.0
```

Keep the `richarddeweerd/` part: the Registry also has two older ESP8266 libraries called ESPManager and espManager, which are something else.

With the Arduino IDE: install the "esp32" boards package by Espressif, version 3.x, from the Boards Manager. Then download the repository as a ZIP from GitHub and add it with *Sketch → Include Library → Add .ZIP Library…*. The library is built and tested with PlatformIO only, so the Arduino IDE is untested.

The library isn't in the Arduino Library Manager yet.

### 2. Build and flash

Put the sketch above in `src/main.cpp` (or open *File → Examples → EspManager → Basic* in the Arduino IDE) and upload it:

```bash
pio run -t upload
```

The library prints nothing unless you ask. To follow along on the serial monitor (`pio device monitor`), add to `setup()`, before `esp.begin()`:

```cpp
Serial.begin(115200);
esp.setLogLevel(espmgr::LogLevel::Info);
```

### 3. Set it up from a phone

A new board has no Saved Network, so it opens the Provisioning Portal:

1. On the phone, join the Wi-Fi network named after the device, such as `esp-abcdef` (`esp-` and the last six hex digits of its MAC address). It's open, without a password.
2. The phone shows "sign in to network" and opens the setup page. If it doesn't, browse to `http://192.168.4.1/`.
3. Pick your network from the list, or "Other network…" for a hidden one, and enter its password.
4. The device tests the network. The phone may drop off the device's access point for a moment, because the device may have to move its access point to the router's channel; stay on the page, it picks up the result when the phone is back.
5. If the device connected, it saves the network and the page says so. If not, the page says why (wrong password, network not found, …), with *Try again* and *Save anyway*, which saves a network that's out of range right now.

The access point closes 30 seconds after setup, and the device carries on on your network. From then on it reconnects by itself after every restart. If the Owner doesn't finish, the access point closes after 10 minutes without a request and opens again.

To start over, see [Resets](#resets-and-restart), or the [Button example](examples/Button).

## Guide

Calls on `esp` live in the `EspManager` class (`src/EspManager.h`); everything else is in `namespace espmgr`. The settings described below are made before `esp.begin()` unless it says otherwise.

### Connecting

`begin()` loads the Saved Networks and returns at once; connecting happens in `process()`. The connection state, `esp.state()`, is one of:

- `Idle`: there's no Saved Network.
- `Connecting`: an attempt is under way.
- `Connected`: connected, with an IP address.
- `Waiting`: waiting to try again after a failure or a lost connection.

```cpp
if (esp.state() == espmgr::ConnectionState::Connected) { /* … */ }
```

Each attempt scans first and joins the first Saved Network on the list that's in range. If none is in range, it tries the first one anyway, in case it's hidden. An attempt gives up after 20 seconds.

After a failure the device waits before trying again: 1 second at first, twice as long after each further failure, up to 5 minutes. It waits a random 75–100 % of that, so devices that lost the same router don't all come back at once. A successful connection starts again at 1 second. To change the limits:

```cpp
esp.setRetryDelays(2000, 60000);  // from 2 s up to 1 min; false unless 0 < min <= max
```

`esp.lastFailure()` says why the last attempt failed or the connection was lost: `None`, `NetworkNotFound`, `WrongPassword`, `Timeout`, `ConnectionLost` or `Other`. `espmgr::toString(esp.lastFailure())` gives a short English text, such as "wrong password".

`begin()` never waits. A sketch that can't do anything useful before it has Wi-Fi can wait on purpose:

```cpp
esp.begin();
if (!esp.waitForConnection(10000))  // keeps calling process(); up to 10 s
{
    // not connected yet; connecting carries on in process()
}
```

`waitForConnection()` returns `true` as soon as the device is connected, and `false` after the timeout, or at once if there's no Saved Network.

### Saved Networks

The device keeps its Saved Networks in flash (NVS), in the Owner's order: it joins the first one on the list that's in range. Both web sites show the list, where the Owner can move networks up and down or remove them. Your sketch can change it through `esp.networks()`:

```cpp
esp.networks().setMaxNetworks(3);              // how many fit: 1 to 20, 5 by default
esp.networks().save("Home", "wifi password");  // an empty password means an open network
esp.networks().move("Home", 1);                // second place; 0 is the top
esp.networks().remove("Old router");
```

- A network saved under a new SSID goes to the top of the list. Saving an SSID that's on the list already updates it in its place.
- `save()` returns `false` if the list is full (`esp.networks().full()`) and the SSID is new, or if the network isn't valid: the SSID must be 1 to 32 bytes; the password empty, 8 to 63 printable ASCII characters, or 64 hex digits.
- A list stored by firmware with a higher limit is kept as it is, but nothing new can be added until it's below the limit.
- Removing the network the device is connected to doesn't disconnect it; the device moves on when that connection ends.
- `count()`, `contains(ssid)` and `list()` read the list. `list()` includes the passwords: never print or log them.
- A network saved while the device is `Idle` is used on the next `process()`.

A Saved Network uses DHCP unless you give it a static address:

```cpp
espmgr::SavedNetwork office;
office.ssid = "Office";
office.password = "wifi password";
office.dhcp = false;
office.staticIp.ip = espmgr::Ipv4Address(192, 168, 1, 50);
office.staticIp.gateway = espmgr::Ipv4Address(192, 168, 1, 1);
office.staticIp.subnet = espmgr::Ipv4Address(255, 255, 255, 0);
office.staticIp.dns1 = espmgr::Ipv4Address(192, 168, 1, 1);  // DNS server; dns2 for a second one
esp.networks().save(office);
```

### Hostname

The device gives the router a name, `esp-` plus the last six hex digits of its MAC address, such as `esp-abcdef`. The Provisioning Portal's access point uses the same name.

```cpp
esp.setHostname("kitchen-lamp");  // instead of the default name
esp.setNamePrefix("lamp");        // keep the MAC part: lamp-abcdef
esp.hostname();                   // the name in use
```

A hostname is 1 to 31 letters, digits and hyphens, not starting or ending with a hyphen; both setters return `false` otherwise. Call them before `begin()`; a later change reaches the router with the next connection.

### Provisioning Portal

The Provisioning Portal opens by itself when there's no Saved Network. `esp.startPortal()` opens it at any other time, from your own button for example, and `esp.stopPortal()` closes it. `esp.portalState()` is `Off` or `Active`. While it's open, the device keeps connecting to its Saved Networks as usual.

The setup page lists the networks in range, strongest first, each SSID once, without hidden networks or ones weaker than −90 dBm. When there are Saved Networks already, the page opens on their list instead, where the Owner can reorder or remove them before adding one. A network chosen there is tested before it's saved, as in the [Quick start](#3-set-it-up-from-a-phone).

The pages only answer on the access point, never on the network the device is connected to. The settings are on `esp.portal()`:

```cpp
esp.portal().setPassword("setup-password");           // WPA2 instead of open; 8 to 63 characters
esp.portal().setSsid("Lamp setup");                   // instead of the device name
esp.portal().setTimeout(5 * 60 * 1000);               // close after 5 min without a request; 0 never
esp.portal().setRestartOnTimeout(false);              // after a timeout, stay closed
esp.portal().setOpenAfterFailingFor(15 * 60 * 1000);  // also open after 15 min without a connection
esp.portal().setCaptivePortal(false);                 // no DNS or redirects: the Owner opens 192.168.4.1
esp.portal().setMinSignal(-80);                       // leave out networks weaker than -80 dBm
```

- `setTimeout()`: 10 minutes by default. Each request from the Owner starts it again.
- `setRestartOnTimeout()`: on by default, so without a Saved Network the portal opens again after a timeout. It opens by itself only once, though: after `stopPortal()` it stays closed until `startPortal()`.
- `setOpenAfterFailingFor()`: off (0) by default. When it's set, the portal also opens after the device has had no connection for that long, and closes once it's connected again.
- `setSsid()`, `setPassword()` and `setCaptivePortal()` take effect the next time the portal opens.

`onProvisioningCompleted` reports the network the Owner saved; the portal closes 30 seconds later, so the page can still show the result.

### Management Interface

The Management Interface is a page on the device's own address in the Owner's network, such as `http://192.168.1.50/`, with these tabs:

- **Wi-Fi**: the Saved Networks, with the one in use marked. The Owner can move or remove a network, and add one. An added network is saved without a test, since testing would drop the Owner's own connection; the Provisioning Portal is the place to test one. The last Saved Network can't be removed here; the Network Reset is for that.
- **Status**: the connection, addresses, signal, hostname, MAC, uptime, why the device last started, free memory, chip and versions, plus *Recent events* from the [Connection History](#connection-history).
- **Settings**: only when you mark a Setting for it; see [Settings](#settings).
- **Device**: restart, Settings Reset, Network Reset and Factory Reset, each after a confirmation.

It's off by default. Turn it on before `begin()`:

```cpp
esp.management().enable();
esp.management().setUser("owner");          // "admin" by default
esp.management().setRealm("Kitchen lamp");  // what the browser's login shows; the hostname by default
esp.begin();
```

With it enabled, the Provisioning Portal asks the Owner for a management password after the Wi-Fi password, and stores it when setup completes. The Management Interface only answers once a password is stored and the device is connected. Browsers ask for the user name and password themselves (HTTP Digest authentication).

If the Owner enters the password some other way, over serial or Bluetooth say, store it with `esp.management().setPassword(password)` (8 to 63 printable ASCII characters). Never hard-code a password in the firmware: every device would share it. Only a hash of the password is stored, and the hash includes the user name and realm, so changing either afterwards means the Owner sets the password again through the Provisioning Portal.

### Settings

Settings are your firmware's own values, for the Owner to fill in on the web pages: a server's name, a port, a choice of unit. The library checks every change, stores the values in NVS and tells you when one changes. Declare them before `esp.begin()`, which reads the stored values:

```cpp
auto& mqtt = esp.settings().section("MQTT");
auto& host = mqtt.addText("mqtt_host", "Server").required();
auto& port = mqtt.addNumber("mqtt_port", "Port", 1883).range(1, 65535);
auto& password = mqtt.addPassword("mqtt_pass", "Password");
auto& unit = esp.settings().addSelect("unit", "Unit", "c").option("c", "Celsius").option("f", "Fahrenheit");
auto& verbose = esp.settings().addCheckbox("verbose", "Extra output");
esp.begin();

Serial.printf("%s:%d\n", host.value().c_str(), static_cast<int>(port.value()));
```

Keep the handles, or look a value up by key: `esp.settings().text("mqtt_host")`, `number("mqtt_port")` or `checkbox("verbose")`. `text()` is also for password and select Settings.

| Declared with | Holds | Options |
| --- | --- | --- |
| `addText(key, label, default)` | a `std::string`, UTF-8 | `.required()`, `.maxLength(n)` |
| `addPassword(key, label)` | a `std::string`, never shown or logged | `.required()`, `.maxLength(n)` |
| `addNumber(key, label, default)` | an `int32_t` | `.range(min, max)` |
| `addCheckbox(key, label, default)` | a `bool` | |
| `addSelect(key, label, default)` | the value of one option, as a `std::string` | `.option(value, label)`, `.required()` |

- A key is 1 to 15 letters, digits, `_` and `-`, and unique. It's also the name in NVS, so changing it loses the stored value.
- Text and passwords are 64 bytes at most unless `.maxLength()` says otherwise, up to 512, and can't hold control characters.
- `esp.settings().section("MQTT")` names a Section. Without one, a Setting goes under a default heading. Settings stay in the order you declare them.
- A mistake in a declaration (a repeated or invalid key, or declaring after `begin()`) can't stop the build: it logs an error and gives a Setting that holds its default, isn't stored or shown, and answers `false` to `.valid()`. Turn on [logging](#logging) before you declare, as [examples/Settings](examples/Settings) does.

For your own check, `.validate()` returns the text to show the Owner, or nothing to accept the value. It isn't called for an empty value, and it's also called for the stored value at `begin()`:

```cpp
esp.settings().addText("room", "Room").validate(
    [](const std::string& value) { return value.size() < 3 ? std::string("Use at least 3 characters.") : std::string(); });
```

Your sketch can change a value too. `set()` makes the same checks, and returns `false` if the value was refused or couldn't be stored, or before `begin()`:

```cpp
auto& interval = esp.settings().addNumber("interval", "Interval (s)", 60).range(5, 3600);
esp.begin();
interval.set(120);
```

Whichever way a value changes, `onSettingChanged` reports it from inside `process()`, once per Setting. It doesn't fire when values load at `begin()`, or when a value is saved unchanged. A password's `text()` is always empty, so printing it this way is safe. A Setting that only takes effect after a restart can call `esp.restart()` here, which waits long enough for the page's answer to go out:

```cpp
esp.onSettingChanged(
    [](const espmgr::Setting& setting)
    {
        Serial.printf("%s is now '%s'\n", setting.key().c_str(), setting.text().c_str());
        if (setting.key() == "mqtt_host")
        {
            esp.restart();
        }
    });
```

**On the web pages.** During first setup, when there's no Saved Network yet, the Provisioning Portal shows one screen per Section after the management password, and the Owner can't go on while a required Setting is empty. The values are stored when setup completes. Adding a network later skips those screens; the list of Saved Networks has a *Settings* entry instead, which saves straight away. A save is all or nothing: if one value is refused, none change.

The Management Interface shows only what you mark, and refuses the rest. The *Settings* tab appears when at least one Setting is marked:

```cpp
esp.settings().addText("display", "Display name").editableInManagement();
```

A password's field stays empty on the pages. Leaving it empty keeps the stored password; a password that isn't required also gets a box to remove it.

**Required Settings** hold up first setup, but never Wi-Fi. A firmware update that adds one doesn't reopen the Provisioning Portal; the pages show a "needs attention" badge, and the sketch can decide what to do:

```cpp
if (!esp.settings().complete())
{
    esp.startPortal();
}
```

**Storage.** A value is stored only when it differs from the default, each under its own key, so a new default in your firmware reaches every device whose Owner never changed that Setting. A stored value that no longer fits, after you narrow a range or remove an option say, reads as the default and stays stored until the Setting is next saved. A Settings Reset (`esp.settingsReset()`) returns everything to the defaults and keeps the Saved Networks.

**Memory.** Measured on an ESP32 and an ESP32-S3, 32 Settings use about 7 KB of heap once declared, or about 8 KB with values stored in 20 of them, and `begin()` takes about 60 ms to read them. That's with labels of about 20 characters; longer labels, defaults and values use more. The figure comes from `test/test_hw_settings`, which prints it on every run.

### Events

To react to what the library does, add listeners. Each `onX()` adds one and returns `esp`, so calls can be chained. Any number can listen to each event, and they're only ever called from inside `process()`, after the change, so they may safely call back into `esp`:

```cpp
esp.onConnected([](const espmgr::ConnectedInfo& info)
                { Serial.printf("Connected to %s as %s\n", info.ssid.c_str(), info.ip.toString().c_str()); })
    .onPortalStarted([](const std::string& ssid) { Serial.printf("Set me up via %s\n", ssid.c_str()); });
```

| Event | Listener arguments | When |
| --- | --- | --- |
| `onConnected` | `const ConnectedInfo&` (`ssid`, `ip`, `channel`) | connected, with an IP address |
| `onDisconnected` | `FailureReason` | an established connection was lost |
| `onConnectFailed` | `const std::string& ssid, FailureReason` | an attempt to join a Saved Network failed |
| `onConnectionStateChanged` | `ConnectionState from, ConnectionState to` | `esp.state()` changed |
| `onPortalStarted` | `const std::string& accessPointSsid` | the Provisioning Portal's access point is up |
| `onPortalStopped` | none | the Provisioning Portal closed |
| `onProvisioningCompleted` | `const std::string& ssid` | the Owner finished setup and the network is saved |
| `onSettingChanged` | `const Setting&` | a Setting has a new value, once per Setting |

Listeners can't be removed.

### Resets and restart

The library reads no buttons; call these from your own button or menu code ([examples/Button](examples/Button) shows one way):

```cpp
esp.networkReset();   // forget every Saved Network and leave the network; the Provisioning Portal opens again
esp.settingsReset();  // Settings back to their defaults
esp.factoryReset();   // erase all of NVS, your own Preferences included, and restart
esp.restart();        // restart a moment later, from process()
```

The Network Reset keeps the management password and the Settings; the Settings Reset keeps the Saved Networks and the management password. The resets return `false` if erasing failed; `factoryReset()` restarts anyway. The Management Interface's Device tab offers all of these too. Its Settings Reset also covers Settings the page doesn't show.

### Status

`esp.status()` returns an `espmgr::DeviceStatus` with everything the status pages show: connection state, SSID, IP address, gateway, DNS server, signal and channel (while connected), hostname, MAC address, uptime, why the device last started, free heap, chip, your firmware version and the library's version. It never holds a password.

`status.resetReason` is `PowerOn`, `External` (the reset button, or a programmer over USB), `Software` (for example `esp.restart()`), `Crash`, `Watchdog`, `Brownout` (the supply voltage dropped too low), `DeepSleep` or `Other`; `espmgr::toString()` gives a short code such as `"brownout"`. A device that keeps restarting with `Brownout` usually needs a better power supply. Set your firmware's version for it with:

```cpp
esp.setFirmwareVersion("1.4.2");
```

The Provisioning Portal's page shows the same under *Status*, and the Management Interface on its Status tab.

### Scanning

Your own code can scan too, without blocking:

```cpp
esp.startScan();  // false if no scan could be started
// later, in loop():
if (esp.scanComplete())
{
    for (const espmgr::ScanResult& network : esp.scanResults())
    {
        Serial.printf("%s %d dBm, channel %u%s\n", network.ssid.c_str(), network.rssi,
                      network.channel, network.secured ? ", secured" : "");
    }
}
```

The list is cleaned up as on the setup page: no hidden networks, each SSID once at its strongest, none weaker than `esp.portal().setMinSignal()`, strongest first. The device's own connection attempts share these scans.

### Logging

The library is silent by default. To see what it does, start `Serial` and pick a level, `Error`, `Warn`, `Info`, `Debug` or `Verbose`:

```cpp
Serial.begin(115200);
esp.setLogLevel(espmgr::LogLevel::Debug);
```

Lines look like `[espmgr 12.345 I wifi] connected to Home as 192.168.1.50, channel 6`. Passwords are never logged, at any level.

To send the lines somewhere else, such as syslog or MQTT, or print them your own way, set a handler. It replaces `Serial`; `nullptr` goes back to it:

```cpp
esp.setLogHandler([](const espmgr::LogEntry& entry) {
    Serial.printf("%s: %s\n", entry.tag, entry.message);
});
esp.setLogLevel(espmgr::LogLevel::Info);
```

An `espmgr::LogEntry` has the `level`, `uptimeMs`, `tag`, `message` and the finished `line`, cut off at 160 characters. Its strings are only valid during the call, so copy what you keep. A handler doesn't turn logging on; the level still decides what's logged. It's called at once from inside the library, from whichever call logged, so keep it short and don't call back into `EspManager` from it.

Only EspManager's own lines go to the handler. The Arduino core and ESP-IDF log through their own system, set with the core's debug level (`-D CORE_DEBUG_LEVEL=…`).

### Connection History

The device keeps its most recent connection events in memory: when it started, each connect (SSID, signal, channel), each failed attempt (SSID and reason) and each lost connection (reason), with the time since start-up:

```cpp
for (const espmgr::ConnectionEvent& event : esp.connectionHistory().entries())  // oldest first
{
    Serial.printf("%llu ms %s %s %s\n", static_cast<unsigned long long>(event.uptimeMs),
                  espmgr::toString(event.kind), event.ssid.c_str(), espmgr::toString(event.reason));
}
```

It keeps the last 20 events; `esp.connectionHistory().setCapacity(n)` before `begin()` changes that, from 0 (keep none) to 50, at about 50 bytes each. A network the Owner tests in the Provisioning Portal isn't recorded unless the device joins it. A restart clears the history, while the Network Reset and Settings Reset leave it; the [status](#status) says why the device last started. The Management Interface shows it on its Status tab as *Recent events*. The Provisioning Portal doesn't, since anyone in range can open it.

### Branding

Both web pages can carry your product's look. Set it at any time; the pages read it each time they load:

```cpp
static const uint8_t logo[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">…</svg>";

esp.ui().setTitle("Acme Thermostat");                         // at most 64 bytes
esp.ui().setLogo(logo, sizeof(logo) - 1, "image/svg+xml");    // or image/png, image/jpeg, image/webp
esp.ui().setTheme(espmgr::Theme::Dark);                       // Auto (follow the phone), Light or Dark
esp.ui().setCss(":root { --accent: #0f766e; }");
```

- **Title**: the heading of both sites, with the built-in one (*Wi-Fi setup*, *Device management*) below it, and the start of the browser tab's title. An empty title goes back to none. A title over 64 bytes or with a control character is refused: the call returns false, logs at `Error` and changes nothing.
- **Logo**: shown next to the title and used as the tab's icon. The bytes aren't copied, so keep them in a `static const` array, as above; `setLogo(nullptr, 0, nullptr)` removes it. Keep it small: SVG is usually a few hundred bytes. Another type is refused like a bad title.
- **Theme**: `Auto`, the default, follows the phone's light or dark setting. There's no switch for the Owner.
- **CSS**: loaded after the pages' own style, so it wins. The string isn't copied either, so use a string literal. Restyle the pages through their custom properties; the class names may change between versions. Rules under `[data-theme="dark"]` apply in the dark theme, whether the phone or `setTheme()` chose it, so put them after your `:root` rule.

| Property | What it colours or sets |
| --- | --- |
| `--accent`, `--accent-text` | buttons, links and highlights, and the text on them |
| `--bg`, `--surface`, `--surface-2` | the page, cards and fields, and secondary buttons |
| `--text`, `--muted`, `--line` | text, secondary text, borders |
| `--ok`, `--warn`, `--bad` | success, warnings and errors |
| `--font` | the font, the system font by default; there's no internet during setup, so web fonts can't load |
| `--radius`, `--radius-sm`, `--space`, `--shadow` | corners, spacing and card shadows |

The pages fetch these from `GET /api/ui`, `GET /custom.css` and `GET /logo`, which the Provisioning Portal serves openly and the Management Interface behind its login.

## Examples

- [Basic](examples/Basic): the smallest sketch, `begin()` and `process()`, with an optional wait for the connection.
- [Settings](examples/Settings): your own settings in two Sections, with validation, a password, some marked for the Management Interface, and `onSettingChanged`.
- [Branding](examples/Branding): a title, an SVG logo, a fixed light theme and a teal accent in your own CSS, on both web pages.
- [Diagnostics](examples/Diagnostics): a log handler printing the library's lines in its own format, and every minute the status, with why the device last started, and the Connection History.
- [Button](examples/Button): the board's BOOT button opens the Provisioning Portal on a short press, starts a Network Reset when held for 5 seconds and a Factory Reset when held for 10, acting when it's let go. It's polled from `loop()` without `delay()`, and the serial monitor says what letting go will do. Build with `-D BUTTON_PIN=<n>` for another pin.

## Supported hardware

- ESP32, ESP32-S3 and ESP32-C3, on Arduino-ESP32 core 3.x. The library is built against 3.3.12 (pioarduino `55.03.312-1`).
- Every change is built for all three, and every change on `main` is tested on a real ESP32 and ESP32-S3. The ESP32-C3 is only built.
- ESP8266 and Arduino-ESP32 2.x are not supported.

## Security notes

- Wi-Fi passwords are stored in the ESP32's NVS flash as plain text. They're only encrypted at rest if you enable the ESP32's flash and NVS encryption.
- The Provisioning Portal's access point is open by default, and it's up while the Owner sets the device up. Anyone in range can use it then. Give it a password with `esp.portal().setPassword()` if that matters for your device; print the password on the device or its box.
- The Management Interface is served over plain HTTP, since a small device can't realistically serve HTTPS. Its HTTP Digest authentication keeps the password itself off the network, but anyone on the same network can read the pages. It assumes a trusted local network. Only a hash of its password is stored.
- Password-type Settings are stored in NVS as plain text too, under the same condition. They never go to a page or the logs, but the sketch can read them.
- Anyone in range of an open Provisioning Portal can read and change your Settings, and can replace or remove a password-type one, though not read it. If your Settings matter, give the portal a password. In the Management Interface, marked Settings are behind its login, and travel over plain HTTP like everything else on it.
- Moving and removing Saved Networks, saving Settings, and on the Management Interface adding a network, restarting and the resets, need a header that only the device's own pages send, so another web site can't make the Owner's browser do them.
- Passwords, password-type Settings included, never appear in the logs, the status or any page the device serves. They never reach a log handler either.
- The Connection History holds the names of the networks the device joined or tried. The Management Interface shows it behind its login; the Provisioning Portal doesn't serve it.

## Development

Building, the tests (on your computer and on a board) and the code layout are described in [CONTRIBUTING.md](CONTRIBUTING.md), with the review checklist.

## Licence

MIT, see [LICENSE](LICENSE).
