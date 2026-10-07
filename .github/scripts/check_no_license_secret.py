#!/usr/bin/env python3
"""check_no_license_secret.py: fail if this public repo carries license-signing key material.

WHY: from 2026-08-10 to 2026-10-07 the ACPL2 native-lane signing secret sat in plaintext in
this public repo, inside the vendored Source/BlueprintAutoLayout/Private/ACPDistTools/ACPLicense.h
(a 48-byte C array). It was rotated, and the key now lives only in a gitignored
ACPLicenseSecret.h next to that header. This check keeps it that way: it runs in CI on every
push / PR, and as a pre-commit hook (.githooks/pre-commit) on the staged snapshot.

It does not need to know the secret. It rejects key-SHAPED material:
  * a tracked file named ACPLicenseSecret.h, secrets.py, acp_license.py or *.license
  * a #define of ACP_LICENSE_SECRET_NATIVE_BYTES, or `_SECRET_* = bytes.fromhex(`
  * any run of >= 16 two-digit hex byte literals (array or macro body) that are not all 0x00
  * any run of >= 64 hex characters (a hex-encoded key of 32+ bytes)
When the private acp-dist-tools secrets.py exists locally (fleet machines only, never CI), it
ALSO compares by value (raw, hex, wrapped hex, C bytes). It never prints secret bytes.

A self-test plants random key-shaped material first; if a detector misses it, the check fails
instead of reporting a meaningless PASS.

Usage:
  check_no_license_secret.py            # scan every git-tracked file (CI)
  check_no_license_secret.py --staged   # scan the staged index snapshot (pre-commit)
Exit 0 = clean, 1 = key material found, 2 = could not run.
"""
import os
import re
import secrets as _rng
import subprocess
import sys

FORBIDDEN_NAMES = {"acplicensesecret.h", "secrets.py", "acp_license.py"}
FORBIDDEN_SUFFIXES = (".license",)
DEFINE_RE = re.compile(rb"#\s*define\s+ACP_LICENSE_SECRET_NATIVE_BYTES\b")
PY_SECRET_RE = re.compile(rb"_SECRET_[A-Z_]*\s*=\s*bytes\.fromhex\s*\(")
BYTERUN_RE = re.compile(rb"(?:0[xX][0-9a-fA-F]{2}\b[\s,\\]*){16,}")
BYTE_RE = re.compile(rb"0[xX]([0-9a-fA-F]{2})\b")
HEXRUN_RE = re.compile(rb"[0-9a-fA-F]{64,}")
MIN_ARRAY_BYTES = 16
SECRETS_PY = os.environ.get("ACP_SECRETS_PY", "/Users/Shared/GH/acp-dist-tools/secrets/secrets.py")


def shape_hits(path, data):
    name = os.path.basename(path).lower()
    if name in FORBIDDEN_NAMES or name.endswith(FORBIDDEN_SUFFIXES):
        yield "forbidden secret-bearing filename"
    if b"\0" in data[:8192]:
        return  # binary asset (png, gif, uasset): not a source of key literals
    if DEFINE_RE.search(data):
        yield "defines the ACP_LICENSE_SECRET_NATIVE_BYTES macro"
    if PY_SECRET_RE.search(data):
        yield "_SECRET_* = bytes.fromhex("
    for run in BYTERUN_RE.finditer(data):
        vals = [int(b, 16) for b in BYTE_RE.findall(run.group(0))]
        if len(vals) >= MIN_ARRAY_BYTES and any(vals):
            yield f"key-shaped byte array ({len(vals)} hex bytes)"
            break
    if HEXRUN_RE.search(data):
        yield "hex run of 64+ characters"


def load_known_secrets():
    if not os.path.isfile(SECRETS_PY):
        return {}
    ns = {}
    try:
        with open(SECRETS_PY) as fh:
            exec(compile(fh.read(), SECRETS_PY, "exec"), ns)  # noqa: S102 - trusted local file
    except Exception as exc:  # a broken local secrets file must not hide a shape hit
        sys.stderr.write(f"note: could not load {SECRETS_PY} for value checks ({exc})\n")
        return {}
    return {k: (v.encode() if isinstance(v, str) else bytes(v))
            for k, v in ns.items() if k.startswith("_SECRET")}


def value_hits(data, known):
    if not known:
        return
    squashed = re.sub(rb"\s+", b"", data).lower()
    all_bytes = bytes(int(b, 16) for b in BYTE_RE.findall(data))
    for name, sec in known.items():
        if sec in data or sec.hex().encode() in squashed or sec in all_bytes:
            yield f"value match for {name}"


def self_test():
    key = _rng.token_bytes(48)
    planted = {
        "a.h": b"static const uint8 K[48] = { " + b", ".join(b"0x%02x" % b for b in key) + b" };",
        "b.txt": key.hex().encode(),
        # split so this file never matches its own #define detector
        "c.h": b"#" + b"define ACP_LICENSE_SECRET_NATIVE_BYTES \\\n 0x01, 0x02",
        "d.h": b"#define KEY \\\n\t" + b", \\\n\t".join(b"0x%02x" % b for b in key),
        "ACPLicenseSecret.h": b"// empty",
    }
    for path, data in planted.items():
        if not list(shape_hits(path, data)):
            return f"detector missed planted material in {path}"
    if list(value_hits(planted["a.h"], {"_SECRET_TEST": key})) == []:
        return "value detector missed a planted C byte array"
    if list(shape_hits("ok.h", b"static const uint8 SecretNative[48] = {\n#if X\n A\n#else\n 0x00\n#endif\n};")):
        return "detector flagged the legitimate zero placeholder"
    return None


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, check=True).stdout


def main():
    staged = "--staged" in sys.argv[1:]
    err = self_test()
    if err:
        print(f"SELF-TEST FAILED: {err}. Refusing to report PASS.", file=sys.stderr)
        return 2
    try:
        if staged:
            listing = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z")
        else:
            listing = git("ls-files", "-z")
    except subprocess.CalledProcessError as exc:
        print(f"could not list files: {exc.stderr.decode(errors='replace')}", file=sys.stderr)
        return 2
    paths = [p.decode() for p in listing.split(b"\0") if p]
    if not paths and not staged:
        print("no tracked files arrived; scanned nothing. Refusing to report PASS.", file=sys.stderr)
        return 2
    known = load_known_secrets()
    hits = []
    for path in paths:
        try:
            data = git("show", f":{path}") if staged else open(path, "rb").read()
        except (OSError, subprocess.CalledProcessError):
            continue
        for why in list(shape_hits(path, data)) + list(value_hits(data, known)):
            hits.append((path, why))
    mode = "staged files" if staged else "tracked files"
    extra = " + value checks against local acp-dist-tools" if known else ""
    if hits:
        for path, why in hits:
            print(f"FAIL: {path}: {why}")
        print(f"\nLicense-signing key material in {len(hits)} place(s) among {len(paths)} {mode}.\n"
              "This repo is PUBLIC. The ACPL2 secret belongs only in the gitignored\n"
              "ACPDistTools/ACPLicenseSecret.h written by acp-dist-tools/sync_dist_tools.sh.\n"
              "Unstage/remove it. If it was ever pushed, the secret must be ROTATED.")
        return 1
    print(f"PASS: no license-signing key material in {len(paths)} {mode} (self-test OK{extra}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
