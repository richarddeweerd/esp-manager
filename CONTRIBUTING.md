# Contributing

Thanks for helping.

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

The web pages are written as plain HTML, CSS and JavaScript in `src/web/<page>/`, with all their text in `src/web/strings.en.json`. `scripts/build-web.py` turns each page into one gzipped file inside a generated header (`src/web/generated/`), which is committed, so building the library needs no extra tools. Run it after changing a page; `scripts/build-web.py --check` fails if a header is out of date. A link to an absolute path, such as `/custom.css`, isn't inlined: the device serves it.

The Time Section's zone picker uses `src/web/common/zones.js`, the named zones of the tz database with their POSIX rules, written by `scripts/build-zones.py` from the computer's own zoneinfo (`/usr/share/zoneinfo` by default). Run it after a tz database update that changes a rule, then `scripts/build-web.py`, and commit both.

The library's logic reaches the hardware only through small interfaces in `src/core/` (`WifiDriver`, `Storage`, `Clock`, `HttpServer`, `LogSink` and `System`). The ESP32 versions are in `src/platform/esp32/`; the unit tests use the fakes in `test/fakes/` instead, which is why they run without a board.

### On-device tests

The tests in `test/test_hw_*/` run on a board. `test_hw_platform` checks the ESP32 versions of those interfaces against real hardware: scanning and connecting, the access point, NVS storage and the web server. `test_hw_espmanager` runs the whole library. Build them and run them on a connected board with

```bash
scripts/build-hw-tests.sh esp32-s3-devkitc-1 hw-firmware
ESPMGR_TEST_SSID=<network> ESPMGR_TEST_PASSWORD=<password> \
    scripts/run-hw-tests.py --port <serial port> --firmware hw-firmware/esp32-s3-devkitc-1/test_hw_platform.bin
```

`run-hw-tests.py` needs `esptool` and `pyserial`. It flashes the board, sends it the Wi-Fi network to test against over the serial port (the password is never printed), checks the board's web server from your computer, so both must be on the same network, and can write a JUnit report with `--junit`. Without a network the tests that need one are ignored. Plain `pio test -e <board>` runs them too, also without the network tests. The ESP32-S3 environment prints to the native USB port.

The test that moves between two Saved Networks also needs `ESPMGR_TEST_SSID_B`, a second network with the same password, and `ESPMGR_TEST_SWITCH`, a command that `run-hw-tests.py` runs with `a` or `b` to make only that network available. Without them it's ignored.

`test_hw_settings` restarts the board halfway, to check that Settings survive on real NVS. Its tests before the restart print their results, and the summary at the end counts only the ones after it; a failure before the restart ends the run there. It also prints what 32 Settings cost in heap and how long reading them takes, which is where the figure in the README comes from.

`test_hw_ota` updates the board over four boots: it refuses an image with a bad signature, installs version 2.0.0 and restarts into the other app slot, confirms it, installs 3.0.0, restarts without confirming it, and checks that the bootloader went back to 2.0.0. `run-hw-tests.py` serves the Manifests and the image (the test's own app image, which `build-hw-tests.sh` leaves in `app/test_hw_ota.bin`) over HTTPS on port 8443, signed with the test-only keys in `test/keys/`, and tells each boot which version it is. The test certificate is for `10.42.0.1`, so the board must reach your computer at that address, as on the test rig, whose Pi is the access point. It's built with the `-ota` environments (`esp32dev-ota` and so on), which use the `min_spiffs.csv` partition table; `build-examples.sh` uses them for `examples/Updates` too. Its run may take up to 15 minutes.

The tests also print every Wi-Fi disconnect with the Wi-Fi stack's reason code and the signal, at the start of each test. `test_hw_espmanager` checks the Connection History after moving between the two networks; it prints the history as `history …` lines.

## Code

- C++17, formatted with the repository's `.clang-format` (`scripts/check-format.sh` checks it).
- `PascalCase` types, `camelCase` functions and methods, everything in `namespace espmgr`.
- Core code in `src/core/` is plain C++. It reaches the hardware only through the interfaces in `src/core/` (`WifiDriver`, `Storage`, `Clock`, `HttpServer`, `LogSink`, `System`, `TimeDriver`, `UpdateDriver`), so it can be tested on a computer. Only `src/platform/` and the on-device tests in `test/test_hw_*/` may include Arduino or ESP-IDF headers (`scripts/check-includes.sh` checks it).
- Nothing in the core blocks: work happens in small steps from `process()`.
- Web pages live in `src/web/<page>/` and keep every piece of text in `src/web/strings.en.json`, so they can be translated later. After changing one, run `scripts/build-web.py` and commit the regenerated header with it (`scripts/build-web.py --check` checks it).
- Every change to core behaviour comes with native tests in `test/`, using the fakes in `test/fakes/`.

## Review checklist

Check each merge request for these:

- [ ] **No password reaches the logger.** Wi-Fi passwords, the Management Interface password and the values of password-type Settings are never passed to `Logger`, not even to a level that's off by default. They aren't masked afterwards, so the only safe rule is not to pass them at all. The same goes for diagnostics and error messages.
- [ ] Input from the web pages is validated before it's stored or handed to the application.
- [ ] Nothing new blocks inside `process()`.
- [ ] Native tests cover the change, and `pio test -e native` passes.
- [ ] Hardware code changes are checked on a board (`test/test_hw_platform`, see [On-device tests](#on-device-tests)).
- [ ] `CHANGELOG.md` has an entry under "Unreleased" for anything a user would notice.
