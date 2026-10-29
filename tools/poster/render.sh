#!/bin/sh
# Render the Tailon Lock poster. Usage: tools/poster/render.sh [--preview] [--variant NAME] [other poster.py options]
# Prefers a Python environment with the bpy 4.5 module (BPY_PYTHON, default ~/bpyenv/bin/python; poster.py needs
# Blender 4.x), then a native "blender". Blender 5.x is not supported. For GPU rendering, put your GPU runtime's
# libraries on LD_LIBRARY_PATH first.
set -e
cd "$(dirname "$0")"
mkdir -p out
PY=${BPY_PYTHON:-$HOME/bpyenv/bin/python}
if [ -x "$PY" ]; then
  exec "$PY" poster.py -- "$@"
elif command -v blender >/dev/null 2>&1; then
  exec blender -b --factory-startup -P poster.py -- "$@"
else
  echo "No Blender 4.x found. Set one up with: uv venv -p 3.11 ~/bpyenv && VIRTUAL_ENV=~/bpyenv uv pip install 'bpy==4.5.*'" >&2
  exit 1
fi
