"""Inline maps_game.js into page.html -> tailonlock-maps.html. Usage: python3 build_page.py <outdir> [carrier_game.js]  (default ../../jets/carrier_game.js, i.e. maps/src -> jets/)"""
import os, sys
here = os.path.dirname(os.path.abspath(__file__)); out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..')
page = open(os.path.join(here, 'page.html')).read()
data = open(os.path.join(out, 'maps_game.js')).read()
carrier = open(os.path.join(here, '..', '..', 'jets', 'carrier_game.js')).read() if len(sys.argv) < 3 else open(sys.argv[2]).read()
carrier = carrier.replace('const CARRIER_TYPE =', 'window.CARRIER_TYPE =').replace("if (typeof module !== 'undefined') module.exports = CARRIER_TYPE;", '')
open(os.path.join(out, 'tailonlock-maps.html'), 'w').write(page.replace('/*MAPS_JS*/', data).replace('/*CARRIER_JS*/', carrier))
print('wrote', os.path.join(out, 'tailonlock-maps.html'))
