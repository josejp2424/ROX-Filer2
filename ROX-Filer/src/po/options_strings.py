#!/usr/bin/env python3
"""Extract user-visible strings from Rox-Filer2 Options.xml for gettext."""
import sys
import xml.etree.ElementTree as ET


def c_quote(text: str) -> str:
    return (text.replace('\\', '\\\\')
                .replace('"', '\\"')
                .replace('\n', '\\n'))


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} Options.xml", file=sys.stderr)
        return 2
    root = ET.parse(sys.argv[1]).getroot()
    seen = set()
    for elem in root.iter():
        values = []
        for attr in ('title', 'label', 'unit'):
            value = elem.attrib.get(attr)
            if value:
                values.append(value.strip())
        if elem.text and elem.text.strip():
            values.append(' '.join(elem.text.split()))
        for value in values:
            if not value or value in seen:
                continue
            seen.add(value)
            print(f'N_("{c_quote(value)}");')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
