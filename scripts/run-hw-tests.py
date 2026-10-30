#!/usr/bin/env python3
"""Flash an on-device test image to a board, run it and report the Unity results.

Usage:
    scripts/run-hw-tests.py --port /dev/esp-s3 --firmware firmware/esp32-s3/test_hw_platform.bin \
        [--junit report.xml] [--name esp32-s3] [--no-flash]

Build the image with scripts/build-hw-tests.sh. The test network is read from the environment
variables ESPMGR_TEST_SSID and ESPMGR_TEST_PASSWORD and sent to the board over the serial port;
without them the tests that need a network are ignored. The password is never printed.

For the tests that move between two networks, ESPMGR_TEST_SSID_B names a second network with the
same password, and ESPMGR_TEST_SWITCH is a command that, given "a" or "b", makes the access point
broadcast the first or the second name (the rig's own script). Without both they are ignored.
The access point is switched back to the first name when the run ends.

Needs esptool (5.x) and pyserial. Exits non-zero when a test fails or the run does not finish.
"""

import argparse
import gzip
import hashlib
import json
import os
import socket
import struct
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

import shlex

import serial

BAUD = 115200
RESULT = re.compile(r"^(?P<file>[^:]+):(?P<line>\d+):(?P<name>\w+):(?P<status>PASS|FAIL|IGNORE)(?::\s?(?P<message>.*))?$")
SUMMARY = re.compile(r"^(?P<tests>\d+) Tests (?P<failures>\d+) Failures (?P<ignored>\d+) Ignored")

# Must match test/test_hw_espmanager/test_main.cpp. A password for the rig's board only.
MANAGEMENT_USER = "admin"
MANAGEMENT_PASSWORD = "rig-management"


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
            if response.headers.get("X-Espmgr") != "rig":
                return "/hw lacks the header set with sendHeader()"

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


def dns_lookup(server, name, timeout_s=5):
    """Asks `server` for the A record of `name` and returns the first address, or None."""
    query = struct.pack(">HHHHHH", 0x4553, 0x0100, 1, 0, 0, 0)
    query += b"".join(bytes([len(part)]) + part.encode() for part in name.split(".")) + b"\0"
    query += struct.pack(">HH", 1, 1)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout_s)
        sock.sendto(query, (server, 53))
        answer, _ = sock.recvfrom(512)
    if len(answer) < 12 or struct.unpack(">H", answer[6:8])[0] == 0:
        return None
    # The last record's data, which for a single A answer is its four address bytes.
    return socket.inet_ntoa(answer[-4:])


def portal_checks(ip):
    """Runs the host side of test_portal_refuses_requests_from_the_test_network: the
    Provisioning Portal only answers on its own access point, and its captive DNS server
    answers every name with the access point's address. Returns "ok" or what failed."""
    try:
        address = dns_lookup(ip, "connectivitycheck.gstatic.com")
    except OSError as error:
        return f"DNS query failed: {error}"
    if address != "192.168.4.1":
        return f"DNS answered {address}, not 192.168.4.1"
    for path in ("/", "/hotspot-detect.html"):
        try:
            with urllib.request.urlopen(f"http://{ip}{path}", timeout=5) as response:
                return f"{path} answered {response.status} on the test network"
        except urllib.error.HTTPError as error:
            if error.code != 403:
                return f"{path} answered {error.code}, not 403"
        except (OSError, urllib.error.URLError) as error:
            return f"{path} failed: {error}"
    return "ok"


def md5(text):
    return hashlib.md5(text.encode()).hexdigest()


def digest_fields(header):
    """The fields of a WWW-Authenticate: Digest header."""
    return dict(re.findall(r'(\w+)="?([^",]*)"?', header.split(" ", 1)[1]))


def digest_get(url, path, password, count, challenge=None):
    """GET `path` with Digest credentials (qop=auth). Asks for a challenge first unless one is
    given. Returns (status, headers, body, challenge)."""
    if challenge is None:
        try:
            urllib.request.urlopen(url + path, timeout=5)
            raise RuntimeError(f"{path} answered without credentials")
        except urllib.error.HTTPError as error:
            if error.code != 401:
                raise RuntimeError(f"{path} without credentials answered {error.code}, not 401")
            challenge = digest_fields(error.headers.get("WWW-Authenticate", ""))
    nc = f"{count:08x}"
    cnonce = "72696721"
    ha1 = md5(f"{MANAGEMENT_USER}:{challenge['realm']}:{password}")
    ha2 = md5(f"GET:{path}")
    response = md5(f"{ha1}:{challenge['nonce']}:{nc}:{cnonce}:auth:{ha2}")
    header = (f'Digest username="{MANAGEMENT_USER}", realm="{challenge["realm"]}", '
              f'nonce="{challenge["nonce"]}", uri="{path}", algorithm=MD5, qop=auth, nc={nc}, '
              f'cnonce="{cnonce}", response="{response}"')
    request = urllib.request.Request(url + path, headers={"Authorization": header})
    try:
        with urllib.request.urlopen(request, timeout=5) as answer:
            return answer.status, answer.headers, answer.read(), challenge
    except urllib.error.HTTPError as error:
        return error.code, error.headers, b"", challenge


def management_checks(ip, hostname):
    """Runs the host side of test_management_interface_logs_the_host_in: the Management
    Interface asks for Digest credentials, refuses a wrong password and a replayed request, and
    serves the status and the page to the right one. Returns "ok" or what failed."""
    url = f"http://{ip}"
    try:
        status, _, _, challenge = digest_get(url, "/api/status", "wrong password", 1)
        if challenge.get("qop") != "auth" or challenge.get("realm") != hostname:
            return f"unexpected challenge {challenge}"
        if status != 401:
            return f"a wrong password got {status}, not 401"

        status, _, body, challenge = digest_get(url, "/api/status", MANAGEMENT_PASSWORD, 1)
        if status != 200:
            return f"the right password got {status} for /api/status"
        data = json.loads(body)
        if data.get("hostname") != hostname or data.get("ip") != ip or \
                data.get("connection") != "Connected":
            return f"/api/status answered {data}"

        status, _, _, _ = digest_get(url, "/api/status", MANAGEMENT_PASSWORD, 1, challenge)
        if status != 401:
            return f"a replayed request got {status}, not 401"

        status, headers, body, _ = digest_get(url, "/", MANAGEMENT_PASSWORD, 2, challenge)
        if status != 200 or headers.get("Content-Encoding") != "gzip":
            return f"/ answered {status} with Content-Encoding {headers.get('Content-Encoding')!r}"
        if '"manageTitle"' not in gzip.decompress(body).decode():
            return "/ is not the Management Interface page"
    except (OSError, RuntimeError, ValueError, urllib.error.URLError) as error:
        return f"request failed: {error}"
    return "ok"


class Switcher:
    """Runs ESPMGR_TEST_SWITCH and remembers whether the access point must be switched back."""

    def __init__(self, command):
        self.command = shlex.split(command) if command else []
        self.on_b = False

    def switch(self, which):
        if which not in ("a", "b"):
            return f"unknown network {which!r}"
        try:
            subprocess.run(self.command + [which], check=True, timeout=60,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        except subprocess.CalledProcessError as error:
            return f"switch {which} failed: {error.stderr.strip() or error.returncode}"
        except (OSError, subprocess.TimeoutExpired) as error:
            return f"switch {which} failed: {error}"
        self.on_b = which == "b"
        return "ok"

    def restore(self):
        if self.on_b:
            print(f"==> Switching the access point back: {self.switch('a')}", flush=True)


def run(conn, ssid, password, ssid_b, switcher, timeout_s):
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
                if ssid and ssid_b:
                    conn.write(f"WIFI\t{ssid}\t{password}\t{ssid_b}\n".encode())
                    print(f"==> Sent test networks {ssid} and {ssid_b}", flush=True)
                elif ssid:
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
            elif line.startswith("ESPMGR-HW PORTAL "):
                outcome = portal_checks(line.split(" ", 2)[2])
                print(f"==> Portal checks: {outcome}", flush=True)
                conn.write(f"PORTAL DONE {outcome}\n".encode())
            elif line.startswith("ESPMGR-HW MGMT "):
                _, _, ip, hostname = line.split(" ", 3)
                outcome = management_checks(ip, hostname)
                print(f"==> Management Interface checks: {outcome}", flush=True)
                conn.write(f"MGMT DONE {outcome}\n".encode())
            elif line.startswith("ESPMGR-HW SWITCH "):
                which = line.split(" ", 2)[2]
                outcome = switcher.switch(which) if switcher.command else "no switch command"
                print(f"==> Switched the access point to {which}: {outcome}", flush=True)
                conn.write(f"SWITCH DONE {outcome}\n".encode())
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
    switcher = Switcher(os.environ.get("ESPMGR_TEST_SWITCH", ""))
    ssid_b = os.environ.get("ESPMGR_TEST_SSID_B", "") if ssid and switcher.command else ""

    start = time.monotonic()
    try:
        with open_port(args.port) as conn:
            if args.no_flash:
                reset(conn)
            results, summary = run(conn, ssid, password, ssid_b, switcher, args.timeout)
    finally:
        switcher.restore()
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
