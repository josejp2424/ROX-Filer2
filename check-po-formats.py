#!/usr/bin/env python3
"""Validate printf placeholders in Rox-Filer2 gettext catalogues.

msgfmt -c remains the authoritative PO syntax check.  This additional gate
catches release-breaking c-format substitutions before .mo files are built.
"""
from __future__ import annotations
import ast
import re
import sys
from pathlib import Path

PRINTF_RE = re.compile(
    r'%(?:(?P<pos>\d+)\$)?'
    r'[-+#0 \'I]*'
    r'(?:\d+|\*)?'
    r'(?:\.(?:\d+|\*))?'
    r'(?:hh|h|ll|l|j|z|t|L)?'
    r'(?P<conv>[diuoxXfFeEgGaAcspn%])'
)


def unquote(line: str) -> str:
    quote = line.find('"')
    if quote < 0:
        return ''
    return ast.literal_eval(line[quote:])


def fields(text: str):
    out = []
    implicit = 1
    for match in PRINTF_RE.finditer(text):
        conv = match.group('conv')
        if conv == '%':
            continue
        pos = int(match.group('pos')) if match.group('pos') else implicit
        if not match.group('pos'):
            implicit += 1
        out.append((pos, conv))
    return out


def entries(path: Path):
    lines = path.read_text(encoding='utf-8').splitlines()
    i = 0
    flags = set()
    while i < len(lines):
        line = lines[i]
        if line.startswith('#,'):
            flags = {x.strip() for x in line[2:].split(',')}
            i += 1
            continue
        if not line.startswith('msgid '):
            if not line.strip():
                flags = set()
            i += 1
            continue

        msgid = unquote(line)
        i += 1
        while i < len(lines) and lines[i].startswith('"'):
            msgid += unquote(lines[i]); i += 1

        plural = None
        if i < len(lines) and lines[i].startswith('msgid_plural '):
            plural = unquote(lines[i]); i += 1
            while i < len(lines) and lines[i].startswith('"'):
                plural += unquote(lines[i]); i += 1

        strings = []
        while i < len(lines) and lines[i].startswith('msgstr'):
            value = unquote(lines[i]); i += 1
            while i < len(lines) and lines[i].startswith('"'):
                value += unquote(lines[i]); i += 1
            strings.append(value)

        yield flags, msgid, plural, strings
        flags = set()


def signature(text: str):
    return sorted(fields(text))


def main(argv):
    if len(argv) < 2:
        print('usage: check-po-formats.py FILE.po [...]', file=sys.stderr)
        return 2
    errors = 0
    for raw in argv[1:]:
        path = Path(raw)
        for flags, msgid, plural, strings in entries(path):
            if 'c-format' not in flags:
                continue
            singular_sig = signature(msgid)
            plural_sig = signature(plural) if plural is not None else singular_sig
            for index, msgstr in enumerate(strings):
                # 2.12.2-82: un msgstr vacio significa "sin traducir", no un
                # error de formato: gettext devuelve el msgid original, asi
                # que los marcadores printf siguen siendo los correctos.
                # Antes bastaba una sola cadena c-format sin traducir para
                # que release-gate.sh fallara en los once catalogos.
                if not msgstr:
                    continue
                expected = singular_sig if plural is None or index == 0 else plural_sig
                got = signature(msgstr)
                if got != expected:
                    print(f'{path}: c-format mismatch', file=sys.stderr)
                    print(f'  msgid: {msgid!r}', file=sys.stderr)
                    if plural is not None:
                        print(f'  plural: {plural!r}', file=sys.stderr)
                    print(f'  msgstr[{index}]: {msgstr!r}', file=sys.stderr)
                    print(f'  expected {expected}, got {got}', file=sys.stderr)
                    errors += 1
    if errors:
        print(f'PO format gate: FAIL ({errors} mismatch(es))', file=sys.stderr)
        return 1
    print('PO format gate: PASS')
    return 0

if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
