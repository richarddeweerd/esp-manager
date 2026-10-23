#!/usr/bin/env python3
"""Check that library.json, library.properties and src/EspManagerVersion.h agree on the version."""
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent


def from_properties():
    m = re.search(r"^version=(.+)$", (root / "library.properties").read_text(), re.M)
    return m.group(1).strip() if m else None


def from_header():
    text = (root / "src" / "EspManagerVersion.h").read_text()
    define = lambda name: re.search(rf"#define\s+{name}\s+(\S+)", text)
    string = define("ESPMGR_VERSION")
    parts = [define(f"ESPMGR_VERSION_{p}") for p in ("MAJOR", "MINOR", "PATCH")]
    if not string or not all(parts):
        return None, None
    return string.group(1).strip('"'), ".".join(p.group(1) for p in parts)


versions = {
    "library.json": json.loads((root / "library.json").read_text()).get("version"),
    "library.properties": from_properties(),
}
versions["ESPMGR_VERSION"], versions["ESPMGR_VERSION_MAJOR/MINOR/PATCH"] = from_header()

for source, version in versions.items():
    print(f"{source:34} {version}")

if None in versions.values() or len(set(versions.values())) != 1:
    sys.exit("Version mismatch: update all of the above together.")
