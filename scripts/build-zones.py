#!/usr/bin/env python3
"""Write src/web/common/zones.js: the named time zones and their POSIX TZ strings.

The Time Section's zone picker (EP18) offers these names and sends the POSIX string of the one
the Owner picks. The names come from the tz database's zone.tab, one or more per country, so
every country's own city is there, plus UTC. Each POSIX string is the footer of the zone's
compiled TZif file, the rule the C library applies after the last listed transition.

    scripts/build-zones.py [zoneinfo dir]   # /usr/share/zoneinfo by default

Run it again after a tz database update that changes a rule, and commit the result.
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "web" / "common" / "zones.js"


def posix_rule(path):
    data = path.read_bytes()
    if not data.startswith(b"TZif") or data[4:5] < b"2":
        sys.exit(f"{path} has no POSIX footer (TZif version 2 or later needed)")
    footer = data.rstrip(b"\n").rsplit(b"\n", 1)[-1].decode("ascii")
    if not footer:
        sys.exit(f"{path} has an empty POSIX footer")
    return footer


def main():
    zoneinfo = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/share/zoneinfo")
    names = set()
    for line in (zoneinfo / "zone.tab").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            names.add(line.split("\t")[2])
    zones = {name: posix_rule(zoneinfo / name) for name in sorted(names)}
    zones["UTC"] = "UTC0"
    version = (zoneinfo / "+VERSION").read_text().strip() if (zoneinfo / "+VERSION").exists() else "?"
    body = json.dumps(zones, separators=(",", ":"), sort_keys=True).replace('","', '",\n"')
    OUT.write_text(
        '"use strict";\n\n'
        f"// Written by scripts/build-zones.py from tz database {version}; don't edit.\n"
        "// Zone name -> POSIX TZ string, for the Time Section's zone picker.\n"
        f"const ZONES = {body};\n",
        encoding="utf-8",
    )
    print(f"{OUT.relative_to(ROOT)}: {len(zones)} zones from tz {version}")


if __name__ == "__main__":
    main()
