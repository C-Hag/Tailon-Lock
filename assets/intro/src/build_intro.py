#!/usr/bin/env python3
"""Add the Tailon Lock opening cinematic to ace4combat.html. Re-appliable: run it on whatever the game file holds now.

usage: build_intro.py <ace4combat.html> <intro dir> [out.html]
<intro dir> holds intro_script.json, intro_assets.json and src/ (intro_core.js, intro.js, music_horizon.js, music_pop.js).
Every edit is anchored on an exact string. The intro block and the Steel Horizon module are replaced if already there; the
script stops if an anchor is missing.
"""
import json, os, re, sys

game, idir = sys.argv[1], sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else game
h = open(game, encoding='utf-8').read()
rd = lambda *p: open(os.path.join(idir, *p), encoding='utf-8').read()
compact = lambda p: json.dumps(json.load(open(os.path.join(idir, p))), separators=(',', ':'))


def sub1(old, new, what):
    global h
    if old not in h:
        sys.exit(f'anchor missing: {what}')
    h = h.replace(old, new, 1)


def once(marker, old, new, what):
    """apply only if marker text is not in the file yet"""
    if marker in h:
        return
    sub1(old, new, what)


# 1. Music: the music work's current modules (music/engines/ in the repo, heavy C64-style mixes) replace whatever is
#    between their markers. Steel Horizon is also the intro's cue, so the game and the intro play the same version.
for name, file in (('horizon', 'music_horizon.js'), ('pop', 'music_pop.js')):
    music = rd('src', file).strip()
    ms, me = f'// ---- {name} music start ----', f'// ---- {name} music end ----'
    if ms not in h or me not in h:
        sys.exit(f'anchor missing: {name} music markers')
    a, b = h.index(ms), h.index(me) + len(me)
    h = h[:a] + music + h[b:]

# 2. the intro module (core + player + data), before the game state
core = rd('src', 'intro_core.js').strip()
player = rd('src', 'intro.js').strip()
player = player.replace('__INTRO_SCRIPT__', compact('intro_script.json')).replace('__INTRO_ASSETS__', compact('intro_assets.json'))
block = core + '\n' + player + '\n'
if '// ---- intro core start ----' in h:
    a = h.index('// ---- intro core start ----'); b = h.index('// ---- intro end ----') + len('// ---- intro end ----\n')
    h = h[:a] + block + h[b:]
else:
    sub1('// ---------- game state ----------\n', block + '\n// ---------- game state ----------\n', 'game state')

# 3. hooks into the game loop, menu and music
once("state === 'intro' || state === 'boot') return;", "function musicUpdate() {\n  const want = settings.sound && settings.music && AU.ctx && AU.ctx.state === 'running';\n  const tr = TRACKS[settings.track] || TRACKS.strike;\n",
     "function musicUpdate() {\n  if (state === 'intro' || state === 'boot') return; // the intro plays its own cue\n  const want = settings.sound && settings.music && AU.ctx && AU.ctx.state === 'running';\n  let tr = TRACKS[settings.track] || TRACKS.strike;\n  if (introKeep && curTrack && state === 'menu') tr = curTrack; // the intro's music carries on under the menu\n", 'musicUpdate')
once('curTrack = null, introKeep', 'let musicVol = -1, curTrack = null;', 'let musicVol = -1, curTrack = null, introKeep = false;', 'musicVol')
once("introKeep = false; settings.track", "$('optTrack').addEventListener('change', e => { settings.track",
     "$('optTrack').addEventListener('change', e => { introKeep = false; settings.track", 'optTrack listener')
once("$('optIntro')", "$('optTrack').addEventListener('change', e => {",
     "$('optIntro').addEventListener('change', e => { settings.intro = e.target.checked; saveSettings(); });\n$('introPlay').addEventListener('click', () => Intro.play());\n$('bootSkip').addEventListener('click', () => Intro.skipToMenu());\n$('optTrack').addEventListener('change', e => {", 'intro listeners')
once("$('optIntro').checked", "$('optTrack').value = settings.track;", "$('optTrack').value = settings.track; $('optIntro').checked = settings.intro !== false;", 'syncMenu')
once('Intro.release();', 'function startGame() {\n  auInit();\n', 'function startGame() {\n  Intro.release(); introKeep = false;\n  auInit();\n', 'startGame')
once('if (Intro.active)', "function render() {\n  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, H);\n", "function render() {\n  if (Intro.active) { Intro.render(); return; }\n  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, H);\n", 'render')
once('Intro.update();', '  netTick(dt);\n  musicUpdate();\n', '  Intro.update();\n  netTick(dt);\n  musicUpdate();\n', 'frame')
once('Intro.boot();', "syncMenu(); resize(); newGame('demo', 'normal');\n", "syncMenu(); resize(); newGame('demo', 'normal');\nIntro.boot();\n", 'startup')
once('window.__intro', 'window.__ace = {', 'window.__intro = Intro;\nwindow.__ace = {', '__ace')

# 4. HTML: boot screen and menu controls
once('id="optIntro"', '<label class="chk"><input type="checkbox" id="optMusic"> Music</label>',
     '<label class="chk"><input type="checkbox" id="optMusic"> Music</label>\n      <label class="chk"><input type="checkbox" id="optIntro"> Intro at start</label>\n      <button class="go alt sm" id="introPlay" type="button">Watch intro</button>', 'menu options')
once('id="boot"', '<div id="pause" class="overlay" hidden>',
     '''<div id="boot" class="overlay" hidden>
  <div class="c64 small" role="dialog" aria-labelledby="bootT">
    <p class="boot">LOAD"TAILON LOCK",8,1<br>SEARCHING FOR TAILON LOCK<br>LOADING<br>READY.<span class="cursor"></span></p>
    <h1 id="bootT" style="font-size:clamp(26px,6vw,40px)">PRESS ANY KEY</h1>
    <p class="note">Plays the intro with sound. Any key or button skips it.</p>
    <div class="btnrow">
      <button class="go" id="bootGo" type="button">Play intro</button>
      <button class="go alt" id="bootSkip" type="button">Skip to menu</button>
    </div>
  </div>
</div>

<div id="pause" class="overlay" hidden>''', 'boot overlay')

open(out, 'w', encoding='utf-8').write(h)
print(f'wrote {out} ({len(h)//1024} KB)', file=sys.stderr)
