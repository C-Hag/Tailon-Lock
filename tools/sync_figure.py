#!/usr/bin/env python3
"""sync_figure.py - copies the figure engine (assets/figure/figure.js) into the browser game (web/index.html), between
its two marker lines, or with --check says whether the copy there is the same. Original code for this project."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'figure', 'figure.js')
WEB = os.path.join(ROOT, 'web', 'index.html')
BEGIN = '// ---- the figure engine: a copy of assets/figure/figure.js (tools/sync_figure.py) ----\n'
END = '// ---- the figure engine ends ----\n'


def engine():
    code = open(SRC).read().replace("if (typeof module !== 'undefined') module.exports = Figure;\n", '')
    return BEGIN + code + END


def main():
    page = open(WEB).read()
    if BEGIN in page:
        a, rest = page.split(BEGIN, 1)
        _, b = rest.split(END, 1)
        current = BEGIN + rest.split(END, 1)[0] + END
    else:
        anchor = 'const Intro = (() => {'
        a, b = page.split(anchor, 1)
        b = anchor + b
        current = ''
    if '--check' in sys.argv:
        same = current == engine()
        print('web/index.html has %s copy of the figure engine' % ('the same' if same else 'an old or no'))
        return 0 if same else 1
    open(WEB, 'w').write(a + engine() + b)
    print('copied the figure engine into web/index.html')
    return 0


if __name__ == '__main__':
    sys.exit(main())
