/* assets.h - finding and loading the data files in assets/ (jets, intro). */
#ifndef TL_ASSETS_H
#define TL_ASSETS_H

#include <stdbool.h>

#include "core/map.h"
#include "core/mesh.h"

struct Json;

/* Finds the assets folder next to the program (or in the source tree for
 * development builds) and loads the jets. Returns false if there is none. */
bool assets_init(void);
/* Full path of a file in assets/, or NULL if the folder was not found. */
const char *assets_path(const char *rel);
/* Parses a JSON file from assets/; free with json_free. */
struct Json *assets_json(const char *rel);
/* {v: [[x, y, z]...], f: [[[i...], "#rrggbb"]...], rad} -> mesh */
bool assets_mesh(const struct Json *j, Mesh *m);
/* Every source mesh (the jets and the carrier) into dir: as .glb files from
 * our writer (glb) or as canonical dumps (.txt). Returns how many, or -1. */
int assets_export_meshes(const char *out_dir, bool glb);
/* The maps listed in assets/maps/maps_index.json, loaded on first use. */
#define MAX_MAPS 8
int         assets_map_count(void);
const char *assets_map_name(int i);
const char *assets_map_id(int i);
int         assets_map_find(const char *id);
const Map  *assets_map(int i);

/* What the hangar shows about a jet, from jets/jets_data.json (the same
 * sheet as the hangar model preview): its story, its numbers, its avionics. */
typedef struct {
  char  bloc[40], era[24], inspired[48], role[72], blurb[420];
  char  radar[96], sensors[128], rcs[48], gun[64], engines[96];
  char  weapons[4][80]; /* "6 x MRM-120 MEDIUM-RANGE ACTIVE-RADAR MISSILE" */
  int   nweapons, crew, roll;
  bool  imperial;
  float g_max, g_min, alpha_max;
  float mach_alt, vmax_sl, supercruise, climb, ceiling, turn_inst, turn_sus, corner, range, radius, takeoff, landing;
  float stat[7]; /* speed, turn, climb, range, field, firepower, stealth (3-10) */
} JetSpec;
/* NULL when the jet has no sheet */
const JetSpec *assets_jet_spec(int jet);

/* "#rrggbb" -> 0xRRGGBB */
unsigned assets_hex(const char *s);

#ifdef TL_FUZZ
/* tests/lf_*.c: read the assets from the fuzzer's own folder; the first map of its index read again */
void       assets_fuzz_dir(const char *d);
const Map *assets_fuzz_map(void);
#endif

void assets_load_hi_jets(void); /* once, when ULTRA first draws */

#endif
