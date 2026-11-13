#!/usr/bin/env python3
"""Sign firmware for EspManager's firmware updates and write its Manifest.

Usage:
  scripts/sign-firmware.py new-key <signing-key.pem>
      Makes a new ECDSA P-256 Signing Key and prints its Public Key, the PEM text for
      esp.updates().setPublicKey(). Keep the Signing Key secret and out of the repository.

  scripts/sign-firmware.py sign --key <signing-key.pem> --version <x.y.z> [options] <firmware.bin>
      Signs the image and writes the Manifest (<channel>.json) for the Update Server:
        --out FILE              where to write the Manifest (default: stable.json next to the image)
        --url URL               the image's URL, absolute or relative to the Manifest
                                (default: the image's file name)
        --notes TEXT            release notes, plain text, at most 1024 bytes
        --notes-file FILE       release notes read from a file
        --minimum-version X     devices older than this refuse the update (TooOld)

The signature is ECDSA P-256 over the SHA-256 of the whole image, DER-encoded and base64, as the
device checks it. Signing uses the openssl command, so no Python packages are needed.
"""

import argparse
import base64
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

MAX_NOTES = 1024
MAX_MANIFEST = 4096
# SemVer 2.0, as the device parses it.
SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def fail(message):
    print(f"sign-firmware: {message}", file=sys.stderr)
    sys.exit(1)


def openssl(*args, data=None):
    try:
        result = subprocess.run(["openssl", *args], input=data, capture_output=True, check=False)
    except FileNotFoundError:
        fail("the openssl command is needed")
    if result.returncode != 0:
        fail(f"openssl {args[0]} failed: {result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def public_key(key_file):
    return openssl("ec", "-in", key_file, "-pubout").decode()


def new_key(args):
    if os.path.exists(args.key_file):
        fail(f"{args.key_file} already exists")
    pem = openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout")
    descriptor = os.open(args.key_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as file:
        file.write(pem)
    print(f"Signing Key written to {args.key_file}. Keep it secret.", file=sys.stderr)
    print("Public Key, for esp.updates().setPublicKey():", file=sys.stderr)
    print(public_key(args.key_file), end="")


def sign(args):
    if not SEMVER.match(args.version):
        fail(f"{args.version!r} is not a SemVer version, such as 1.2.0")
    if args.minimum_version and not SEMVER.match(args.minimum_version):
        fail(f"{args.minimum_version!r} is not a SemVer version")
    notes = args.notes or ""
    if args.notes_file:
        with open(args.notes_file, encoding="utf-8") as file:
            notes = file.read().strip()
    if len(notes.encode()) > MAX_NOTES:
        fail(f"the notes are longer than {MAX_NOTES} bytes")

    with open(args.image, "rb") as file:
        image = file.read()
    if not image:
        fail(f"{args.image} is empty")
    digest = hashlib.sha256(image).hexdigest()

    signature = openssl("dgst", "-sha256", "-sign", args.key, args.image)

    # Check the signature against the Public Key, as the device will.
    with tempfile.TemporaryDirectory() as folder:
        public_file = os.path.join(folder, "public.pem")
        signature_file = os.path.join(folder, "signature.der")
        with open(public_file, "w", encoding="ascii") as file:
            file.write(public_key(args.key))
        with open(signature_file, "wb") as file:
            file.write(signature)
        openssl("dgst", "-sha256", "-verify", public_file, "-signature", signature_file, args.image)

    manifest = {
        "version": args.version,
        "url": args.url or os.path.basename(args.image),
        "size": len(image),
        "sha256": digest,
        "signature": base64.b64encode(signature).decode(),
    }
    if args.minimum_version:
        manifest["minimumVersion"] = args.minimum_version
    if notes:
        manifest["notes"] = notes

    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    if len(text.encode()) > MAX_MANIFEST:
        fail(f"the Manifest is longer than {MAX_MANIFEST} bytes")

    out = args.out or os.path.join(os.path.dirname(os.path.abspath(args.image)), "stable.json")
    with open(out, "w", encoding="utf-8") as file:
        file.write(text)
    print(f"Manifest for {args.version} ({len(image)} bytes) written to {out}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    make = commands.add_parser("new-key", help="make a new Signing Key")
    make.add_argument("key_file")
    make.set_defaults(run=new_key)

    signing = commands.add_parser("sign", help="sign an image and write its Manifest")
    signing.add_argument("image")
    signing.add_argument("--key", required=True)
    signing.add_argument("--version", required=True)
    signing.add_argument("--out")
    signing.add_argument("--url")
    notes = signing.add_mutually_exclusive_group()
    notes.add_argument("--notes")
    notes.add_argument("--notes-file")
    signing.add_argument("--minimum-version")
    signing.set_defaults(run=sign)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
