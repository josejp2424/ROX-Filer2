#!/usr/bin/env python3
"""Fail if a runtime PO catalogue contains fuzzy or empty translations.

This is intentionally dependency-free so the release gate does not need polib.
It understands ordinary gettext msgid/msgstr entries and plural msgstr[n]
continuations well enough for the Rox-Filer2 catalogues.
"""

import ast
import re
import sys
from pathlib import Path


def unquote(token):
    try:
        return ast.literal_eval(token.strip())
    except (ValueError, SyntaxError):
        raise ValueError("invalid PO string literal: %s" % token.rstrip())


def parse_po(path):
    entries = []
    entry = None
    state = None

    def ensure():
        nonlocal entry
        if entry is None:
            entry = {"comments": [], "msgid": None, "msgstr": None, "plural": {}}
        return entry

    def flush():
        nonlocal entry, state
        if entry is not None and entry["msgid"] is not None:
            entries.append(entry)
        entry = None
        state = None

    for raw in path.read_text(encoding="utf-8").splitlines() + [""]:
        if not raw.strip():
            flush()
            continue
        current = ensure()
        if raw.startswith("#"):
            current["comments"].append(raw)
            continue
        match = re.match(r"msgid\s+(.*)$", raw)
        if match:
            current["msgid"] = unquote(match.group(1))
            state = ("msgid", None)
            continue
        match = re.match(r"msgstr\s+(.*)$", raw)
        if match:
            current["msgstr"] = unquote(match.group(1))
            state = ("msgstr", None)
            continue
        match = re.match(r"msgstr\[(\d+)\]\s+(.*)$", raw)
        if match:
            index = int(match.group(1))
            current["plural"][index] = unquote(match.group(2))
            state = ("plural", index)
            continue
        if raw.startswith('"') and state:
            value = unquote(raw)
            field, index = state
            if field == "plural":
                current["plural"][index] = current["plural"].get(index, "") + value
            else:
                current[field] = (current[field] or "") + value
    return entries


def check(path):
    untranslated = []
    fuzzy = []
    entries = parse_po(path)
    for entry in entries:
        msgid = entry["msgid"] or ""
        if not msgid:  # gettext header
            continue
        if any("fuzzy" in line for line in entry["comments"] if line.startswith("#,")):
            fuzzy.append(msgid)
        if entry["plural"]:
            if not entry["plural"] or any(not text for text in entry["plural"].values()):
                untranslated.append(msgid)
        elif not entry["msgstr"]:
            untranslated.append(msgid)
    return len(entries) - 1, untranslated, fuzzy


def main(argv):
    if len(argv) < 2:
        print("usage: check-po-completeness.py FILE.po ...", file=sys.stderr)
        return 2
    failed = False
    for name in argv[1:]:
        path = Path(name)
        total, untranslated, fuzzy = check(path)
        if untranslated or fuzzy:
            failed = True
            print("%s: %d entries, %d untranslated, %d fuzzy" %
                  (path, total, len(untranslated), len(fuzzy)), file=sys.stderr)
            for msgid in untranslated[:10]:
                print("  untranslated: %r" % msgid, file=sys.stderr)
            for msgid in fuzzy[:10]:
                print("  fuzzy: %r" % msgid, file=sys.stderr)
        else:
            print("%s: %d/%d translated, 0 fuzzy" % (path.name, total, total))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
