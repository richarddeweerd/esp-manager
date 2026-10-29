#!/usr/bin/env python3
"""Flash an on-device test image to a board, run it and report the Unity results.

Usage:
    scripts/run-hw-tests.py --port /dev/esp-s3 --firmware firmware/esp32-s3/test_hw_platform.bin \
        [--junit report.xml] [--name esp32-s3] [--no-flash]

Build the image with scripts/build-hw-tests.sh. The test network is read from the environment
variables ESPMGR_TEST_SSID and ESPMGR_TEST_PASSWORD and sent to the board over the serial port;
without them the tests that need a network are ignored. The password is never printed.

Needs esptool (5.x) and pyserial. Exits non-zero when a test fails or the run does not finish.
"""

import argparse
import gzip
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

import serial

BAUD = 115200
RESULT = re.compile(r"^(?P<file>[^:]+):(?P<line>\d+):(?P<name>\w+):(?P<status>PASS|FAIL|IGNORE)(?::\s?(?P<message>.*))?$")
SUMMARY = re.compile(r"^(?P<tests>\d+) Tests (?P<failures>\d+) Failures (?P<ignored>\d+) Ignored")

# Must match test/test_hw_platform/test_main.cpp.
REALM_USER = "admin"
REALM_PASSWORD = "hw-check"


def flash(port, firmware):
    print(f"==> Flashing {firmware} to {port}", flush=True)
    subprocess.run(
        [sys.executable, "-m", "esptool", "--port", port, "--baud", "460800",
         "write-flash", "0x0", firmware],
        check=True,
    )


def open_port(port, timeout_s=15):
    """Opens the port without touching DTR/RTS, which would reset the board or put it into
    download mode. A USB-Serial/JTAG port disappears for a moment after a reset, so retry."""
    deadline = time.monotonic() + timeout_s
    while True:
        conn = serial.Serial()
        conn.port = port
        conn.baudrate = BAUD
        conn.timeout = 0.2
        conn.dtr = False
        conn.rts = False
        try:
            conn.open()
            return conn
        except serial.SerialException:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)


def reset(conn):
    conn.dtr = False
    conn.rts = True
    time.sleep(0.1)
    conn.rts = False


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def http_checks(ip):
    """Runs the host side of test_http_server_answers_the_host. Returns "ok" or what failed."""
    base = f"http://{ip}"
    try:
        with urllib.request.urlopen(f"{base}/hw?name=rig", timeout=5) as response:
            body = response.read().decode()
            if response.status != 200 or body != "name=rig":
                return f"/hw answered {response.status} {body!r}"

        try:
            urllib.request.urlopen(f"{base}/hw-auth", timeout=5)
            return "/hw-auth answered without credentials"
        except urllib.error.HTTPError as error:
            if error.code != 401:
                return f"/hw-auth without credentials answered {error.code}, not 401"

        passwords = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        passwords.add_password(None, base, REALM_USER, REALM_PASSWORD)
        digest = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(passwords))
        with digest.open(f"{base}/hw-auth", timeout=5) as response:
            body = response.read().decode()
            if response.status != 200 or body != "authorized":
                return f"/hw-auth with credentials answered {response.status} {body!r}"

        with urllib.request.urlopen(f"{base}/page", timeout=5) as response:
            encoding = response.headers.get("Content-Encoding", "")
            if response.status != 200 or encoding != "gzip":
                return f"/page answered {response.status} with Content-Encoding {encoding!r}"
            page = gzip.decompress(response.read()).decode()
            if not page.startswith("<!doctype html>") or "const STRINGS = {" not in page:
                return "/page is not the generated portal page"

        no_redirect = urllib.request.build_opener(NoRedirect)
        try:
            no_redirect.open(f"{base}/nowhere", timeout=5)
            return "/nowhere was not redirected"
        except urllib.error.HTTPError as error:
            location = error.headers.get("Location", "")
            if error.code != 302 or not location.endswith("/hw"):
                return f"/nowhere answered {error.code} to {location!r}"
    except (OSError, urllib.error.URLError) as error:
        return f"request failed: {error}"
    return "ok"


def run(conn, ssid, password, timeout_s):
    """Talks to the board until Unity prints its summary. Returns (results, summary)."""
    results = []
    summary = None
    answered = False
    reset_sent = False
    start = time.monotonic()
    buffer = b""
    while time.monotonic() - start < timeout_s:
        # No sign of life: the board may have started before the port was open.
        if not answered and not reset_sent and time.monotonic() - start > 8:
            print("==> No answer, resetting the board", flush=True)
            reset(conn)
            reset_sent = True

        buffer += conn.read(conn.in_waiting or 1)
        while b"\n" in buffer:
            raw, buffer = buffer.split(b"\n", 1)
            line = raw.decode(errors="replace").rstrip("\r")
            print(line, flush=True)

            if line == "ESPMGR-HW READY" and not answered:
                if ssid:
                    conn.write(f"WIFI\t{ssid}\t{password}\n".encode())
                    print(f"==> Sent test network {ssid}", flush=True)
                else:
                    conn.write(b"NOWIFI\n")
                    print("==> No test network set; network tests will be ignored", flush=True)
                answered = True
            elif line.startswith("ESPMGR-HW HTTP "):
                outcome = http_checks(line.split(" ", 2)[2])
                print(f"==> HTTP checks: {outcome}", flush=True)
                conn.write(f"HTTP DONE {outcome}\n".encode())
            elif (match := RESULT.match(line)):
                results.append(match.groupdict())
            elif (match := SUMMARY.match(line)):
                summary = {k: int(v) for k, v in match.groupdict().items()}
                return results, summary
    return results, summary


def write_junit(path, name, results, summary, elapsed):
    suite = ET.Element("testsuite", name=name, time=f"{elapsed:.1f}", tests=str(len(results)),
                       failures=str(sum(r["status"] == "FAIL" for r in results)),
                       skipped=str(sum(r["status"] == "IGNORE" for r in results)))
    for result in results:
        case = ET.SubElement(suite, "testcase", name=result["name"], classname=name,
                             file=result["file"], line=result["line"])
        if result["status"] == "FAIL":
            ET.SubElement(case, "failure", message=result["message"] or "failed")
        elif result["status"] == "IGNORE":
            ET.SubElement(case, "skipped", message=result["message"] or "ignored")
    if summary is None:
        case = ET.SubElement(suite, "testcase", name="run_finished", classname=name)
        ET.SubElement(case, "failure", message="the board did not finish the test run")
    ET.ElementTree(suite).write(path, encoding="unicode", xml_declaration=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True)
    parser.add_argument("--firmware", help="flash image to write at offset 0")
    parser.add_argument("--no-flash", action="store_true", help="run what is already on the board")
    parser.add_argument("--junit", help="write a JUnit XML report here")
    parser.add_argument("--name", default="hw", help="test suite name in the report")
    parser.add_argument("--timeout", type=int, default=300, help="seconds for the whole run")
    args = parser.parse_args()

    if not args.no_flash:
        if not args.firmware:
            parser.error("--firmware is required unless --no-flash is given")
        flash(args.port, args.firmware)

    ssid = os.environ.get("ESPMGR_TEST_SSID", "")
    password = os.environ.get("ESPMGR_TEST_PASSWORD", "")

    start = time.monotonic()
    with open_port(args.port) as conn:
        if args.no_flash:
            reset(conn)
        results, summary = run(conn, ssid, password, args.timeout)
    elapsed = time.monotonic() - start

    if args.junit:
        write_junit(args.junit, args.name, results, summary, elapsed)

    if summary is None:
        print(f"==> The board did not finish within {args.timeout} s", flush=True)
        return 1
    print(f"==> {summary['tests']} tests, {summary['failures']} failures, "
          f"{summary['ignored']} ignored", flush=True)
    return 1 if summary["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
