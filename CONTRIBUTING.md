# Contributing

Thanks for helping. Building and running the tests is described in the [README](README.md#building-and-testing).

## Code

- C++17, formatted with the repository's `.clang-format` (`scripts/check-format.sh` checks it).
- `PascalCase` types, `camelCase` functions and methods, everything in `namespace espmgr`.
- Core code in `src/core/` is plain C++. It reaches the hardware only through the interfaces in `src/core/` (`WifiDriver`, `Storage`, `Clock`, `HttpServer`, `LogSink`), so it can be tested on a computer. Only `src/platform/` and the on-device tests in `test/test_hw_*/` may include Arduino or ESP-IDF headers (`scripts/check-includes.sh` checks it).
- Nothing in the core blocks: work happens in small steps from `process()`.
- Every change to core behaviour comes with native tests in `test/`, using the fakes in `test/fakes/`.

## Review checklist

Check each merge request for these:

- [ ] **No password reaches the logger.** Wi-Fi passwords, the Management Interface password and the values of password-type Settings are never passed to `Logger`, not even to a level that's off by default. They aren't masked afterwards, so the only safe rule is not to pass them at all. The same goes for diagnostics and error messages.
- [ ] Input from the web pages is validated before it's stored or handed to the application.
- [ ] Nothing new blocks inside `process()`.
- [ ] Native tests cover the change, and `pio test -e native` passes.
- [ ] Hardware code changes are checked on a board (`test/test_hw_platform`, see the README).
- [ ] `CHANGELOG.md` has an entry under "Unreleased" for anything a user would notice.
