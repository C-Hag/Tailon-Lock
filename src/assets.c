/* assets.c - the data files in assets/ (see assets/README.md): the jets
 * (jets/jets_game.js, the game-format meshes and hangar scores), the carrier,
 * the maps and the intro. */
#include "assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "core/glb.h"
#include "units.h"
#include "core/jets.h"
#include "ff/tlh.h"
#include "ff/tlj.h"
#include "ff/tlpk.h"
#include "platform/plat.h"

static char dir[1024];

static bool is_dir_with(const char *d, const char *file) {
  char p[2100];
  snprintf(p, sizeof p, "%.1000s%.1000s", d, file);
  return plat_is_file(p);
}

/* An asset file, as plat_load_file gives it, and the floppy build's packed ones unpacked on the way: TLPK
 * (src/ff/tlpk.c), TLH terrain heights (src/ff/tlh.c), and inside TLPK TLJ, our binary JSON, turned back into
 * JSON text (src/ff/tlj.c). A file that is
 * neither comes as it is, so the full build and the floppy one read the same way. */
static void *asset_load(const char *path, size_t *size) {
  size_t n = 0;
  uint8_t *b = plat_load_file(path, &n);
  if (b && tlpk_is(b, n)) {
    size_t un = 0;
    uint8_t *u = tlpk_unpack(b, n, (size_t)64 << 20, &un);
    free(b);
    if (!u) plat_log("assets: %s is a damaged packed file", path);
    b = u, n = un;
  }
  if (b && tlh_is(b, n)) { /* terrain heights */
    size_t hn = 0;
    uint8_t *u = tlh_unpack(b, n, &hn);
    free(b);
    if (!u) plat_log("assets: %s is a damaged heights file", path);
    b = u, n = hn;
  }
  if (b && tlj_is(b, n)) {
    size_t tn = 0;
    char *t = tlj_to_text(b, n, &tn);
    free(b);
    if (!t) plat_log("assets: %s is a damaged binary JSON file", path);
    b = (uint8_t *)t, n = tn;
  }
  if (size) *size = n;
  return b;
}

const char *assets_path(const char *rel) {
  static char p[1400];
  if (!dir[0]) return NULL;
  snprintf(p, sizeof p, "%s%s", dir, rel);
  return p;
}


Json *assets_json(const char *rel) {
  const char *p = assets_path(rel);
  if (!p) return NULL;
  size_t n = 0;
  char *text = asset_load(p, &n);
  if (!text) {
    plat_log("assets: cannot read %s", p);
    return NULL;
  }
  Json *j = json_parse(text, n, NULL);
  free(text);
  if (!j) plat_log("assets: %s is not valid JSON", p);
  return j;
}

unsigned assets_hex(const char *s) {
  if (!s) return 0;
  if (*s == '#') s++;
  return (unsigned)strtoul(s, NULL, 16);
}

bool assets_mesh(const Json *j, Mesh *m) {
  const Json *V = json_get(j, "v"), *F = json_get(j, "f");
  const Json *rad = json_get(j, "rad");
  if (!json_is_array(V) || !json_is_array(F)) return false;
  mesh_reset(m, json_is_number(rad) ? (float)rad->num : 10);
  const Json *v;
  JSON_FOR(v, V) {
    if (json_count(v) < 3) return false;
    mesh_vert(m, (float)json_number(json_at(v, 0), 0), (float)json_number(json_at(v, 1), 0),
              (float)json_number(json_at(v, 2), 0));
  }
  const Json *f;
  JSON_FOR(f, F) {
    const Json *idx = json_at(f, 0), *col = json_at(f, 1);
    int ii[FACE_MAX_V], n = 0;
    const Json *k;
    JSON_FOR(k, idx) {
      if (n < FACE_MAX_V) ii[n] = json_int(k, 0);
      n++;
    }
    if (n <= FACE_MAX_V) mesh_face(m, ii, n, assets_hex(json_str(col)));
  }
  return m->nf > 0;
}

static const char *str(const Json *o, const char *k) {
  const char *s = json_str(json_get(o, k));
  return s ? s : "";
}
static float num(const Json *o, const char *k, float def) {
  const Json *n = json_get(o, k);
  return json_is_number(n) ? (float)n->num : def;
}

/* The game-format table in a .js file (const X = [...] or {...}). */
static Json *json_in_js(const char *rel, char open, char close) {
  const char *p = assets_path(rel);
  if (!p) return NULL;
  size_t n = 0;
  char *text = asset_load(p, &n);
  if (!text) return NULL;
  char *a = strchr(text, open), *b = strrchr(text, close);
  Json *j = a && b && b > a ? json_parse(a, (size_t)(b - a + 1), NULL) : NULL;
  free(text);
  if (!j) plat_log("assets: cannot parse %s", p);
  return j;
}

/* Far LOD by vertex clustering, as the browser game's jets patch makes it:
 * snap vertices to a grid, average each cell, drop collapsed and repeated faces. */
static void decimate(const Mesh *src, Mesh *dst, float cell) {
  mesh_reset(dst, src->rad);
  int *map = malloc(sizeof(int) * (size_t)src->nv);
  int *key = malloc(sizeof(int) * 3 * (size_t)src->nv);
  float *acc = calloc((size_t)src->nv, 4 * sizeof(float));
  if (!map || !key || !acc) { /* out of memory: the far LOD stays empty */
    free(map), free(key), free(acc);
    return;
  }
  int nk = 0;
  for (int i = 0; i < src->nv; i++) {
    int k[3] = { (int)floorf(src->v[i].x / cell), (int)floorf(src->v[i].y / cell), (int)floorf(src->v[i].z / cell) };
    int found = -1;
    for (int q = 0; q < nk; q++)
      if (key[3 * q] == k[0] && key[3 * q + 1] == k[1] && key[3 * q + 2] == k[2]) { found = q; break; }
    if (found < 0) {
      found = nk++;
      memcpy(&key[3 * found], k, sizeof k);
    }
    map[i] = found;
    acc[4 * found] += src->v[i].x;
    acc[4 * found + 1] += src->v[i].y;
    acc[4 * found + 2] += src->v[i].z;
    acc[4 * found + 3] += 1;
  }
  for (int q = 0; q < nk; q++) mesh_vert(dst, acc[4 * q] / acc[4 * q + 3], acc[4 * q + 1] / acc[4 * q + 3], acc[4 * q + 2] / acc[4 * q + 3]);
  for (int i = 0; i < src->nf; i++) {
    const Face *f = &src->f[i];
    if (f->n != 3) continue;
    int t[3] = { map[f->i[0]], map[f->i[1]], map[f->i[2]] };
    if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
    bool dup = false;
    for (int k = 0; k < dst->nf && !dup; k++) {
      const Face *g = &dst->f[k];
      int same = 0;
      for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) same += g->i[a] == t[b];
      dup = same == 3;
    }
    if (!dup) mesh_face(dst, t, 3, f->c);
  }
  free(map);
  free(key);
  free(acc);
}

/* Menu order and one-line roles (the browser game's picker has the first four). */
static const struct { const char *id, *role; } JET_ORDER[] = {
  { "ja46", "Swedish-style canard fighter. The slowest of the four: a tough, old-school interceptor." },
  { "lf27", "Light single-engine fighter. Nimble, cheap and quick to climb." },
  { "ks55", "Big twin-engine Flanker-style jet. Best turner, long legs." },
  { "sy26", "Stealthy canard heavyweight. Fast, climbs well, longest range." },
  { "af40", "Stealth air-dominance fighter. The fastest climber; strong in every way but range." },
  { "ecf90", "Agile canard-delta air superiority fighter. Turns and climbs well, short legs." },
  { "vk33", "Frontline point-defence dogfighter. Quick and agile, with the shortest range." },
  { "nf24", "Two-seat swing-wing carrier interceptor. Fast with long legs, but a slow turner." },
};

static JetSpec specs[MAX_JETS];

/* The sheet's text in the bitmap font's characters: x for the times sign,
 * plain digits and dashes, "about" for the tilde-like signs. "about" is longer
 * than what it stands for, so the text is cut to the field's size (n). */
static void plain(char *s, size_t size) {
  static const struct { const char *u, *a; } MAP[] = {
    { "\xc3\x97", "x" }, { "\xc2\xb2", "2" }, { "\xe2\x89\x88", "ABOUT " }, { "~", "ABOUT " }, { "\xe2\x80\x93", "-" }, { "\xe2\x80\x94", "-" },
    { "\xe2\x88\x92", "-" }, { "\xe2\x80\x99", "'" }, { "\xc2\xb7", "-" },
  };
  char out[512];
  size_t n = 0;
  for (const char *p = s; *p;) {
    const char *add = p;
    size_t lu = 1, la = 1;
    for (size_t m = 0; m < sizeof MAP / sizeof MAP[0]; m++)
      if (!strncmp(p, MAP[m].u, strlen(MAP[m].u))) {
        add = MAP[m].a, lu = strlen(MAP[m].u), la = strlen(MAP[m].a);
        break;
      }
    if (n + la >= size || n + la >= sizeof out) break;
    memcpy(out + n, add, la);
    n += la;
    p += lu;
  }
  out[n] = 0;
  memcpy(s, out, n + 1);
}
static bool    have_spec[MAX_JETS];

const JetSpec *assets_jet_spec(int jet) { return jet >= 0 && jet < MAX_JETS && have_spec[jet] ? &specs[jet] : NULL; }

static void load_specs(void) {
  Json *root = assets_json("jets/jets_data.json");
  const Json *j;
  JSON_FOR(j, root) {
    int k = jets_find(str(j, "id"));
    if (k < 0 || k >= MAX_JETS) continue;
    JetSpec *s = &specs[k];
    snprintf(s->bloc, sizeof s->bloc, "%s", str(j, "bloc"));
    snprintf(s->era, sizeof s->era, "%s", str(j, "era"));
    snprintf(s->inspired, sizeof s->inspired, "%s", str(j, "inspired"));
    snprintf(s->role, sizeof s->role, "%s", str(j, "role"));
    snprintf(s->blurb, sizeof s->blurb, "%s", str(j, "blurb"));
    snprintf(s->radar, sizeof s->radar, "%s", str(j, "radar"));
    snprintf(s->rcs, sizeof s->rcs, "%s", str(j, "rcs"));
    snprintf(s->gun, sizeof s->gun, "%s", str(j, "gun"));
    snprintf(s->engines, sizeof s->engines, "%s", str(j, "engines"));
    const Json *e;
    int n = 0;
    JSON_FOR(e, json_get(j, "sensors")) {
      const char *t = json_str(e);
      if (t) n += snprintf(s->sensors + n, sizeof s->sensors - (size_t)n, "%s%s", n ? ", " : "", t);
      if (n >= (int)sizeof s->sensors) break;
    }
    JSON_FOR(e, json_get(j, "weapons")) {
      if (s->nweapons >= 4) break;
      snprintf(s->weapons[s->nweapons++], sizeof s->weapons[0], "%d X %s",
               (int)json_num(json_at(e, 1)), json_str(json_at(e, 0)));
    }
    s->crew = (int)num(j, "crew", 1);
    s->roll = (int)num(j, "roll", 200);
    s->imperial = !strcmp(str(j, "units"), "imperial");
    s->g_max = num(j, "g_max", 9);
    s->g_min = num(j, "g_min", -3);
    s->alpha_max = num(j, "alpha_max", 25);
    char *txt[] = { s->bloc, s->era, s->inspired, s->role, s->blurb, s->radar, s->rcs, s->gun, s->engines, s->sensors,
                    s->weapons[0], s->weapons[1], s->weapons[2], s->weapons[3] };
    const size_t size[] = { sizeof s->bloc, sizeof s->era, sizeof s->inspired, sizeof s->role, sizeof s->blurb,
                            sizeof s->radar, sizeof s->rcs, sizeof s->gun, sizeof s->engines, sizeof s->sensors,
                            sizeof s->weapons[0], sizeof s->weapons[0], sizeof s->weapons[0], sizeof s->weapons[0] };
    for (size_t q = 0; q < sizeof txt / sizeof txt[0]; q++) plain(txt[q], size[q]);
    const Json *p = json_get(j, "perf");
    s->mach_alt = num(p, "m_max_alt", 0);
    s->vmax_sl = num(p, "v_max_sl_kmh", 0);
    s->supercruise = num(p, "supercruise", 0);
    s->climb = num(p, "climb_ms", 0);
    s->ceiling = num(p, "ceiling_m", 0);
    s->turn_inst = num(p, "turn_instant", 0);
    s->turn_sus = num(p, "turn_sustained", 0);
    s->corner = num(p, "corner_kmh", 0);
    s->range = num(p, "range_km", 0);
    s->radius = num(p, "radius_km", 0);
    s->takeoff = num(p, "takeoff_m", 0);
    s->landing = num(p, "landing_m", 0);
    JETS[k].s_vmax = s->vmax_sl / KMH_PER_MPS;
    JETS[k].s_corner = s->corner / KMH_PER_MPS;
    JETS[k].s_tw = num(p, "tw", 0);
    JETS[k].s_roll = s->roll * PI_F / 180;
    JETS[k].nogun = !strncmp(str(j, "gun"), "None", 4);
    /* induced drag over the weight at corner speed, per g squared:
     * K (W/S) / q, doubled to land the sheet's sustained turn rates */
    float ws = num(p, "wing_loading", 0) * 9.81f;
    if (ws > 0 && JETS[k].s_corner > 0)
      JETS[k].s_induced = 4 * (float)num(j, "k", 0) * ws / (1.225f * JETS[k].s_corner * JETS[k].s_corner);
    const Json *g = json_get(j, "game");
    static const char *const STAT[7] = { "speed", "turn", "climb", "range", "field", "firepower", "stealth" };
    for (int q = 0; q < 7; q++) s->stat[q] = num(g, STAT[q], 5);
    JETS[k].stealth = s->stat[6];
    have_spec[k] = true;
  }
  json_free(root);
}

/* A mesh from assets/<rel>, a .glb read by our own loader (core/glb.c); false
 * if there is no such file or it is not a mesh we can read. */
static bool glb_mesh_file(const char *rel, Mesh *m) {
  const char *p = assets_path(rel);
  if (!p) return false;
  size_t n = 0;
  void *data = asset_load(p, &n);
  if (!data) return false;
  GlbModel g;
  const char *err = NULL;
  bool ok = glb_load(data, n, &g, &err) && glb_mesh(&g, m, &err);
  if (g.doc) glb_close(&g);
  free(data);
  if (!ok) plat_log("assets: %s: %s", p, err ? err : "not readable");
  return ok;
}

static int meshes_from_glb; /* how many meshes came from .glb files */

/* a source mesh: assets/jets/<id>.glb when there is one, else the JSON mesh */
static bool source_mesh(const char *id, const Json *json_mesh, Mesh *m) {
  char rel[64];
  snprintf(rel, sizeof rel, "jets/%s.glb", id);
  if (glb_mesh_file(rel, m)) {
    meshes_from_glb++;
    return true;
  }
  return assets_mesh(json_mesh, m);
}

static void load_jets(void) {
  Json *root = json_in_js("jets/jets_game.js", '[', ']');
  if (!root) return;
  static Mesh near, far; /* scratch, reused for every jet */
  for (size_t o = 0; o < sizeof JET_ORDER / sizeof JET_ORDER[0]; o++) {
    const Json *j;
    JSON_FOR(j, root) {
      if (strcmp(str(j, "id"), JET_ORDER[o].id) != 0) continue;
      const Json *g = json_get(j, "game");
      if (!source_mesh(JET_ORDER[o].id, json_get(j, "mesh"), &near)) break;
      float minz = 0;
      for (int i = 0; i < near.nv; i++) minz = fminf(minz, near.v[i].z);
      decimate(&near, &far, 1.1f);
      int k = jets_add(JET_ORDER[o].id, str(j, "designation"), str(j, "name"), JET_ORDER[o].role, num(g, "speed", 6),
                       num(g, "turn", 6), num(g, "climb", 6), num(g, "range", 6), -minz, &near, &far);
      const Json *fl = json_get(j, "flight");
      if (k >= 0) {
        JETS[k].g_max = num(fl, "g_max", 9);
        JETS[k].g_min = num(fl, "g_min", -3);
      }
    }
  }
  json_free(root);
  load_specs();
  plat_log("assets: %d jets (%d meshes from .glb)", NJETS, meshes_from_glb);
}

/* ---- maps --------------------------------------------------------------- */
static struct { char id[24], name[32]; Map *map; bool failed; } maps[MAX_MAPS];
static int nmaps;

int assets_map_count(void) { return nmaps; }
const char *assets_map_name(int i) { return i >= 0 && i < nmaps ? maps[i].name : "CLASSIC"; }
const char *assets_map_id(int i) { return i >= 0 && i < nmaps ? maps[i].id : "classic"; }
int assets_map_find(const char *id) {
  for (int i = 0; i < nmaps; i++)
    if (!strcmp(maps[i].id, id)) return i;
  return -1;
}

static void load_map_index(void) {
  Json *root = assets_json("maps/maps_index.json");
  const Json *e;
  JSON_FOR(e, root) {
    if (nmaps >= MAX_MAPS) break;
    snprintf(maps[nmaps].id, sizeof maps[0].id, "%s", str(e, "id"));
    snprintf(maps[nmaps].name, sizeof maps[0].name, "%s", str(e, "name"));
    for (char *c = maps[nmaps].name; *c; c++)
      if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    nmaps++;
  }
  json_free(root);
}

/* A base's runway runs north-south: the landing circuit, the refuelling stop and the take-off run all work along
 * z. A map's heading is taken as the nearer of 0 and 180 (the spawn uses it too, so jets start on the runway the
 * rest of the game knows), and another one is reported. */
static float runway_heading(float h, const char *map, int team) {
  float n = fm_cosf(h * PI_F / 180) >= 0 ? 0.0f : 180.0f;
  if (h != n && h != n - 360 && h != n + 360) plat_log("map %s: base %d's heading %g taken as %g (runways run north-south)", map, team, (double)h, (double)n);
  return n;
}

/* a[i] as a number: 0 when the array is short or missing (json_at gives NULL then) */
static float num_at(const Json *a, int i) { return (float)json_number(json_at(a, i), 0); }

static vec3 vec_of(const Json *a) {
  return v3(num_at(a, 0), num_at(a, 1), num_at(a, 2));
}

static const char *const TOD_KEYS[TOD_COUNT] = { "dawn", "day", "dusk", "night" };
static const char *const WX_KEYS[WX_COUNT] = { "clear", "scattered", "overcast", "storm" };

static int target_type(const char *t) {
  if (!strcmp(t, "sam")) return TGT_SAM;
  if (!strcmp(t, "fuel")) return TGT_FUEL;
  if (!strcmp(t, "radar")) return TGT_RADAR;
  if (!strcmp(t, "hangar")) return TGT_HANGAR;
  if (!strcmp(t, "ship")) return TGT_SHIP;
  return -1;
}

static bool load_map(int idx, Map *m) {
  char rel[96];
  snprintf(rel, sizeof rel, "maps/%s.json", maps[idx].id);
  Json *j = assets_json(rel);
  if (!j) return false;
  snprintf(m->id, sizeof m->id, "%s", maps[idx].id);
  snprintf(m->name, sizeof m->name, "%s", maps[idx].name);
  snprintf(m->blurb, sizeof m->blurb, "%s", str(j, "blurb"));

  const Json *hm = json_get(j, "heightmap");
  const Json *org = json_get(hm, "origin");
  m->origin_x = num_at(org, 0);
  m->origin_z = num_at(org, 1);
  m->spacing = num(hm, "spacing", 200);
  float scale = num(hm, "scale", 0.1f);
  snprintf(rel, sizeof rel, "maps/%s", str(hm, "file"));
  size_t n = 0;
  uint8_t *raw = asset_load(assets_path(rel), &n);
  if (!raw || n < MAP_SAMPLES * MAP_SAMPLES * 2) {
    free(raw);
    json_free(j);
    return false;
  }
  for (int i = 0; i < MAP_SAMPLES * MAP_SAMPLES; i++) m->h[i] = (float)(int16_t)(raw[2 * i] | raw[2 * i + 1] << 8) * scale;
  free(raw);
  snprintf(rel, sizeof rel, "maps/%s", str(json_get(j, "surface"), "file"));
  raw = asset_load(assets_path(rel), &n);
  if (!raw || n < MAP_CELLS * MAP_CELLS) {
    free(raw);
    json_free(j);
    return false;
  }
  memcpy(m->mat, raw, MAP_CELLS * MAP_CELLS);
  free(raw);

  const Json *e;
  JSON_FOR(e, json_get(j, "palette")) {
    int id = (int)num(e, "id", (float)m->npal);
    if (id < 0 || id >= MAP_MAX_PAL) continue;
    m->pal[id].color = assets_hex(str(e, "color"));
    m->pal[id].water = !strcmp(str(e, "kind"), "water");
    m->pal[id].jitter = num(e, "jitter", 0);
    if (id + 1 > m->npal) m->npal = id + 1;
  }
  const Json *wl = json_get(j, "water_level");
  m->sea = json_is_number(wl);
  m->water_level = m->sea ? (float)wl->num : 40;
  m->water_sky_mix = num(j, "water_sky_mix", 0.25f);
  m->far_ground = assets_hex(str(j, "sky_far_ground"));

  const Json *tods = json_get(j, "time_of_day");
  for (int t = 0; t < TOD_COUNT; t++) {
    const Json *o = json_get(tods, TOD_KEYS[t]);
    TimeOfDay *d = &m->tod[t];
    d->sky_top = assets_hex(str(o, "sky_top"));
    d->sky_hor = assets_hex(str(o, "sky_horizon"));
    d->fog = assets_hex(str(o, "fog"));
    d->sun_color = assets_hex(str(o, "sun_color"));
    d->ambient = assets_hex(str(o, "ambient"));
    d->water_tint = assets_hex(str(o, "water_tint"));
    d->fog_density = num(o, "fog_density", 1);
    d->sun_intensity = num(o, "sun_intensity", 1);
    d->ambient_intensity = num(o, "ambient_intensity", 0.7f);
    d->stars = num(o, "stars", 0);
    d->lights = num(o, "lights", 0);
    d->sun = v3_norm(vec_of(json_get(o, "sun")));
  }

  const Json *wx = json_get(j, "weather");
  const Json *pre = json_get(wx, "presets");
  m->default_weather = WX_SCATTERED;
  for (int w = 0; w < WX_COUNT; w++) {
    if (!strcmp(str(wx, "default"), WX_KEYS[w])) m->default_weather = w;
    const Json *o = json_get(pre, WX_KEYS[w]);
    Weather *W = &m->weather[w];
    W->visibility_km = num(o, "visibility_km", 30);
    W->sun_factor = num(o, "sun_factor", 1);
    W->ambient_factor = num(o, "ambient_factor", 1);
    const Json *cl = json_get(o, "clouds");
    W->nclouds = json_count(cl);
    W->clouds = W->nclouds ? calloc((size_t)W->nclouds, sizeof(Cloud)) : NULL;
    int k = 0;
    const Json *c;
    JSON_FOR(c, cl) {
      Cloud *C = &W->clouds[k++];
      C->x = num(c, "x", 0);
      C->z = num(c, "z", 0);
      C->base = num(c, "base", 1000);
      C->top = num(c, "top", 2000);
      const char *genus = str(c, "genus");
      C->albedo = (!strcmp(genus, "Cb") ? 0.92f : !strcmp(genus, "Fs") ? 0.62f : 1.0f) * (w == WX_STORM ? 0.72f : 1);
      const Json *pl = json_get(c, "puffs");
      C->npuffs = json_count(pl);
      C->puffs = calloc((size_t)(C->npuffs ? C->npuffs : 1), sizeof(Puff));
      int q = 0;
      const Json *pf;
      JSON_FOR(pf, pl) {
        Puff *P = &C->puffs[q++];
        P->dx = num_at(pf, 0);
        P->dy = num_at(pf, 1);
        P->dz = num_at(pf, 2);
        P->r = num_at(pf, 3);
        P->tier = json_count(pf) > 4 ? json_int(json_at(pf, 4), 0) : 0;
      }
    }
  }

  JSON_FOR(e, json_get(j, "bases")) {
    int t = (int)num(e, "team", 0) & 1;
    m->bases[t].x = num(e, "x", 0);
    m->bases[t].z = num(e, "z", 0);
    m->bases[t].elev = num(e, "elev", 0);
    m->bases[t].heading = runway_heading(num(e, "heading", 0), m->id, t);
  }
  JSON_FOR(e, json_get(j, "carriers")) {
    if (m->ncarriers >= MAP_MAX_CARRIERS) break;
    Carrier *c = &m->carriers[m->ncarriers++];
    c->team = (int)num(e, "team", 0);
    c->x = num(e, "x", 0);
    c->z = num(e, "z", 0);
    c->heading = num(e, "heading", 0);
    c->length = num(e, "length", 320);
    c->beam = num(e, "beam", 86);
    c->deck = num(e, "deck_height", 20.5f);
  }
  JSON_FOR(e, json_get(j, "targets")) {
    int type = target_type(str(e, "type"));
    double team = num(e, "team", 0);
    if (type < 0 || (team != 0 && team != 1) || m->ntargets >= MAP_MAX_TGT) continue; /* a team is 0 or 1 */
    MapTarget *t = &m->targets[m->ntargets++];
    t->team = (int)team;
    t->type = type;
    t->x = num(e, "x", 0);
    t->z = num(e, "z", 0);
    t->heading = num(e, "heading", 0);
  }
  bool got[2] = { false, false };
  JSON_FOR(e, json_get(j, "spawns")) {
    int t = (int)num(e, "team", 0) & 1;
    if (got[t] || strcmp(str(e, "kind"), "air") != 0) continue;
    got[t] = true;
    m->spawn[t].x = num(e, "x", 0);
    m->spawn[t].y = num(e, "y", 1500);
    m->spawn[t].z = num(e, "z", 0);
    m->spawn[t].heading = num(e, "heading", 0);
  }
  const Json *tk = json_get(j, "tanker");
  m->tanker_x = num(tk, "x", 0);
  m->tanker_z = num(tk, "z", 0);
  m->tanker_r = num(tk, "radius", 3200);
  m->tanker_alt = num(tk, "alt", 2300);

  const Json *lc = json_get(j, "light_colors");
  const Json *ls = json_get(j, "lights");
  m->nlights = json_count(ls);
  m->lights = calloc((size_t)(m->nlights ? m->nlights : 1), sizeof(MapLight));
  int k = 0;
  JSON_FOR(e, ls) {
    MapLight *L = &m->lights[k++];
    L->x = num(e, "x", 0);
    L->y = num(e, "y", 0);
    L->z = num(e, "z", 0);
    L->size = num(e, "size", 1);
    L->blink = num(e, "blink", 0);
    L->color = assets_hex(json_str(json_get(lc, str(e, "kind"))));
    if (!L->color) L->color = 0xffffff;
  }
  json_free(j);
  plat_log("map %s: %d targets, %d carriers, %d lights", m->id, m->ntargets, m->ncarriers, m->nlights);
  return true;
}

const Map *assets_map(int i) {
  if (i < 0 || i >= nmaps || maps[i].failed) return NULL;
  if (!maps[i].map) {
    Map *m = calloc(1, sizeof *m);
    if (!m || !load_map(i, m)) {
      free(m);
      maps[i].failed = true;
      return NULL;
    }
    maps[i].map = m;
  }
  return maps[i].map;
}

static void load_carrier(void) {
  Json *root = json_in_js("jets/carrier_game.js", '{', '}');
  if (root && source_mesh(str(root, "id"), json_get(root, "mesh"), &MESH_CARRIER))
    plat_log("assets: carrier, %d faces", MESH_CARRIER.nf);
  json_free(root);
}

bool assets_init(void) {
  const char *base = plat_base_path();
  char cand[4][1024];
  int n = 0;
  /* tests (tools/ff/qcheck): A4C_ASSETS, a folder with a trailing slash, is the assets folder, first */
  const char *test_dir = getenv("A4C_ASSETS");
  if (test_dir && *test_dir) snprintf(cand[n++], sizeof cand[0], "%.1000s", test_dir);
  if (base) snprintf(cand[n++], sizeof cand[0], "%sassets/", base);
#ifdef A4C_SOURCE_DIR
  snprintf(cand[n++], sizeof cand[0], "%s/assets/", A4C_SOURCE_DIR);
#endif
  snprintf(cand[n++], sizeof cand[0], "assets/");
  for (int i = 0; i < n; i++)
    if (is_dir_with(cand[i], "jets/jets_game.js")) {
      snprintf(dir, sizeof dir, "%.1000s", cand[i]);
      break;
    }
  if (!dir[0]) {
    plat_log("assets: no assets folder found; flying the built-in jet only");
    return false;
  }
  plat_log("assets: %s", dir);
  load_jets();
  load_carrier();
  load_map_index();
  return true;
}

/* ---- exporting the meshes (brick 7) --------------------------------------- */

/* one source mesh: <dir>/<id>.glb from our writer, or <dir>/<id>.txt, the canonical dump */
static bool export_one(const char *out_dir, const char *id, const Json *mj, bool glb) {
  static Mesh m;
  if (!assets_mesh(mj, &m)) return false;
  char path[1200];
  snprintf(path, sizeof path, "%s/%s.%s", out_dir, id, glb ? "glb" : "txt");
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  bool ok = true;
  if (glb) {
    size_t n;
    uint8_t *b = glb_write_mesh(&m, id, &n);
    ok = b && fwrite(b, 1, n, f) == n;
    free(b);
  } else mesh_dump(&m, f);
  return fclose(f) == 0 && ok;
}

int assets_export_meshes(const char *out_dir, bool glb) {
  int n = 0;
  Json *root = json_in_js("jets/jets_game.js", '[', ']');
  const Json *j;
  JSON_FOR(j, root) {
    if (!export_one(out_dir, str(j, "id"), json_get(j, "mesh"), glb)) {
      json_free(root);
      return -1;
    }
    n++;
  }
  json_free(root);
  root = json_in_js("jets/carrier_game.js", '{', '}');
  if (root && !export_one(out_dir, str(root, "id"), json_get(root, "mesh"), glb)) n = -1;
  else if (root) n++;
  json_free(root);
  return n;
}

/* ---- fuzzing (tests/lf_song.c, tests/lf_map.c) ---------------------------------- */
#ifdef A4C_FUZZ
void assets_fuzz_dir(const char *d) { snprintf(dir, sizeof dir, "%.1000s", d); }

static void free_map(Map *m) {
  if (!m) return;
  for (int w = 0; w < WX_COUNT; w++) {
    for (int c = 0; c < m->weather[w].nclouds && m->weather[w].clouds; c++) free(m->weather[w].clouds[c].puffs);
    free(m->weather[w].clouds);
  }
  free(m->lights);
  free(m);
}

const Map *assets_fuzz_map(void) {
  for (int i = 0; i < nmaps; i++) free_map(maps[i].map);
  memset(maps, 0, sizeof maps);
  nmaps = 0;
  load_map_index();
  return assets_map(0);
}
#endif
