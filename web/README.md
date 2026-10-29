# Tailon Lock in the browser

`index.html` is the whole browser game in one file: the menu with **Play on: Computer / Mobile**, touch controls, the opening film and the control-room scene, all drawn in code. `sfx/` holds the recorded sound effects and `music/` the Full Burn DMX10 theme.

Open `index.html` from a web server (the sounds and music are fetched, which browsers block for `file://`), for example:

    cd web && python3 -m http.server 8000

then go to http://localhost:8000 and choose Mobile on a phone or tablet.

The page is built by the web patch scripts (mobile, sound, intro, control room, clean names) on top of the browser source, in that order.
