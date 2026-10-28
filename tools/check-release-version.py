#!/usr/bin/env python3
"""Prevent packaging a Meson binary with a different Rox-Filer2 version."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
meson = (root / "meson.build").read_text(encoding="utf-8")
appinfo = (root / "ROX-Filer/AppInfo.xml").read_text(encoding="utf-8")
m = re.search(r"(?m)^\s*version\s*:\s*'([^']+)'\s*,?", meson)
a = re.search(r"<Version>\s*([^<\s]+)\s*</Version>", appinfo)
if not m or not a:
    sys.exit("ERROR: unable to read the release version from meson.build or AppInfo.xml")
if m.group(1) != a.group(1):
    sys.exit(f"ERROR: version mismatch: meson.build={m.group(1)} AppInfo.xml={a.group(1)}")
print(f"Rox-Filer2 release version OK: {m.group(1)}")
