#!/usr/bin/env python3
"""Extrae los textos traducibles de Options.xml y los escribe en src/tips.

Portado a Python 3 en 2.12.2-82.

Este script seguia siendo Python 2 (print como sentencia, string.strip,
attrs.has_key).  Como update-po encadena los pasos con && dentro de una
subshell cuyo resultado nadie comprueba, fallaba en la primera linea y la
extraccion completa se abortaba en silencio: por eso messages.pot quedo
desincronizado del codigo y catorce cadenas visibles (_OK, _Yes, _No,
Restore, Add to Bookmarks, About Rox-Filer2 y otras) nunca llegaron a los
once catalogos, que aun asi declaraban 1564/1564 traducidas.
"""

import os
import sys
from xml.sax import parse
from xml.sax.handler import ContentHandler


class Handler(ContentHandler):
    def __init__(self, out):
        super().__init__()
        self.out = out
        self.data = ""

    def startElement(self, tag, attrs):
        for key in ("title", "label", "end", "unit"):
            if key in attrs:
                self.trans(attrs[key])
        self.data = ""

    def characters(self, data):
        self.data += data

    def endElement(self, tag):
        data = self.data.strip()
        if data:
            self.trans(data)
        self.data = ""

    def trans(self, data):
        data = "\\n".join(data.split("\n"))
        if data:
            self.out.write('_("%s")\n' % data.replace('"', '\\"'))


def main():
    print("Extracting translatable bits from Options.xml...")
    try:
        os.chdir("po")
    except OSError:
        pass

    options = os.path.join("..", "..", "Options.xml")
    if not os.path.exists(options):
        sys.stderr.write("ERROR: no se encuentra %s\n" % options)
        return 1

    with open(os.path.join("..", "tips"), "w", encoding="utf-8") as out:
        parse(options, Handler(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
