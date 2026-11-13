#!/usr/bin/env python3
"""sfx_convert.py - turns the effect songs written by our SID toolchain's effect script (one plain-text song per
effect, one row per PAL frame) into the game's own effect format, assets/sfx/NAME.fxs (docs/SFX_FORMAT.md): the
same statements, the toolchain's own headers dropped, and how the game plays it (once for a length, or a loop of a
number of frames) added at the top. Original code for this project.

usage: sfx_convert.py SRC_DIR [OUT_DIR]"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# how each effect plays in the game: a loop of N frames, or once for S seconds
PLAY = {
    'engine-idle': 'loop 384', 'engine-cruise': 'loop 384', 'engine-afterburner': 'loop 192', 'cannon-loop': 'loop 16',
    'radar-search': 'loop 112', 'lock-tone': 'loop 64', 'missile-warning': 'loop 64',
    'missile-launch': 'once 3.0', 'flares': 'once 1.8', 'explosion': 'once 2.3', 'explosion-big': 'once 7.6', 'crash': 'once 6.5',
    'catapult': 'once 3.1', 'landing': 'once 3.9',
}
KEEP = re.compile(r'^(volume|wave|pulse|filter|instrument|command|track)\b')


def convert(text, name):
    out, desc = ['tl-sfx 1', 'play ' + PLAY[name]], ''
    for line in text.split('\n'):
        if line.startswith('# ' + name + ' - '):
            desc = line[len('# ' + name + ' - '):]
        if not line.strip() or line.startswith('#'):
            continue
        if line.startswith(' ') or KEEP.match(line):
            out.append(line.rstrip())
    return '# %s: %s\n' % (name, desc) + '\n'.join(out) + '\n'


def main():
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'assets', 'sfx')
    os.makedirs(dst, exist_ok=True)
    for name in sorted(PLAY):
        with open(os.path.join(src, name + '.sf2txt')) as f:
            text = convert(f.read(), name)
        with open(os.path.join(dst, name + '.fxs'), 'w') as f:
            f.write(text)
    print('wrote %d effects to %s' % (len(PLAY), dst))


if __name__ == '__main__':
    main()
