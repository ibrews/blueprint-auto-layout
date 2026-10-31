#!/usr/bin/env python3
"""Build one Fab-ready plugin zip per supported engine version.

Fab's technical review requires, per engine version: a .uplugin EngineVersion
key, a PlatformAllowList on every module matching the listed target platforms,
a copyright notice with the publisher name in every source file, a
FilterPlugin.ini entry for files outside the standard layout, and no license
files. The repo itself stays MIT with an unpinned EngineVersion (so GitHub users
can load it on any engine); only the Fab packages get these changes.

Usage: python3 Tools/package_fab.py <out_dir> [--ref HEAD]
"""
import argparse
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PUBLISHER_NOTICE = "// Copyright (c) 2026 Alex Coulombe Presents (alexctoys). All rights reserved."
# Platforms runtime-verified per engine version (see README compatibility matrix).
VERSIONS = {
    "5.4": ["Win64"],
    "5.5": ["Win64"],
    "5.6": ["Win64"],
    "5.7": ["Win64", "Mac"],
    "5.8": ["Win64", "Mac"],
}
INCLUDE_PREFIXES = ("Config/", "Resources/", "Source/", "BlueprintAutoLayout.uplugin", "README.md")
OLD_NOTICE = re.compile(r"^// Copyright \(c\) 2026 Alex Coulombe\. Licensed under the MIT License\.\s*$")
TOP_FOLDER = "BlueprintAutoLayout"


def git_files(ref):
    out = subprocess.check_output(["git", "-C", str(REPO), "ls-tree", "-r", "--name-only", ref], text=True)
    return [f for f in out.splitlines() if f.startswith(INCLUDE_PREFIXES)]


def git_show(ref, path):
    return subprocess.check_output(["git", "-C", str(REPO), "show", f"{ref}:{path}"])


def with_notice(path, data):
    if not path.endswith((".h", ".cpp", ".cs")):
        return data
    lines = data.decode("utf-8").split("\n")
    if lines and OLD_NOTICE.match(lines[0]):
        lines[0] = PUBLISHER_NOTICE
    elif PUBLISHER_NOTICE not in lines[:3]:
        lines = [PUBLISHER_NOTICE] + lines
    return "\n".join(lines).encode("utf-8")


def package(version, platforms, ref, out_dir):
    zip_path = out_dir / f"BlueprintAntiPasta-v{json.loads(git_show(ref, 'BlueprintAutoLayout.uplugin'))['VersionName']}-UE{version}.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for path in git_files(ref):
            data = git_show(ref, path)
            if path == "BlueprintAutoLayout.uplugin":
                plugin = json.loads(data)
                plugin["EngineVersion"] = f"{version}.0"
                plugin["SupportedTargetPlatforms"] = platforms
                for module in plugin["Modules"]:
                    module["PlatformAllowList"] = platforms
                data = (json.dumps(plugin, indent=4, ensure_ascii=False) + "\n").encode("utf-8")
            elif path == "Config/FilterPlugin.ini":
                data = data.rstrip(b"\n") + b"\n/README.md\n"
            else:
                data = with_notice(path, data)
            z.writestr(f"{TOP_FOLDER}/{path}", data)
    return zip_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--ref", default="HEAD")
    args = ap.parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for version, platforms in VERSIONS.items():
        print(package(version, platforms, args.ref, out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
