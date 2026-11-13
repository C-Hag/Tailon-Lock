# Assets

Game data the game loads at run time, made by the project's own tools. The build copies this folder next to the program.

- `jets/jets_game.js`: the eight jets in the game's mesh format (+X right
  wing, +Y up, +Z nose, metres) with their hangar scores. The game reads the
  JSON table inside it and builds the far LOD itself. `jets_data.json` has
  the full specs; `carrier_game.js` and `carrier_data.json` are the CVN-90
  Stormhaven and its deck.
- `maps/`: the four maps. `FORMAT.md` describes them. Each map has a `.json`
  (layout, time of day, weather, lights), a `.h16` (16-bit heightmap) and a
  `.mat` (material per cell); `maps_index.json` lists them in menu order.
- `intro/`: the opening cinematic. `INTRO.md` describes `intro_script.json`
  and `intro_assets.json`.

The files here are the finished outputs the game loads; the tools that made
them are not part of this repository.
