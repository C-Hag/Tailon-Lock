/* assets.c - the data files in assets/ (see assets/README.md): the jets
 * (jets/jets_game.js, the game-format meshes and hangar scores), the carrier,
 * the maps and the intro. */
#include "assets.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "core/jets.h"
#include "core/json.h"

static char dir[1024];

static bool is_dir_with(const char *d, const char *file) {
  char p[2100];
  snprintf(p, sizeof p, "%.1000s%.1000s", d, file);
  SDL_PathInfo info;
  return SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_FILE;
}

const char *assets_path(const char *rel) {
  static char p[1400];
  if (!dir[0]) return NULL;
  snprintf(p, sizeof p, "%s%s", dir, rel);
  return p;
}

#ifndef NDEBUG
/* Brick 3 (docs/BUILD_PLAN.md): in debug builds every asset is read by both
 * readers, cJSON and our own (src/core/json.c), and the two trees must match
 * value for value: the same types, names, order, strings and numbers (bit for
 * bit). A mismatch stops the game, naming the file and the path into it. */
static bool same_tree(const cJSON *a, const Json *b, char *path, size_t cap) {
  size_t at = strlen(path);
  bool ok;
  if (cJSON_IsNull(a)) ok = b->type == JSON_NULL;
  else if (cJSON_IsTrue(a)) ok = b->type == JSON_TRUE;
  else if (cJSON_IsFalse(a)) ok = b->type == JSON_FALSE;
  else if (cJSON_IsNumber(a)) ok = b->type == JSON_NUMBER && memcmp(&a->valuedouble, &b->num, sizeof b->num) == 0;
  else if (cJSON_IsString(a)) ok = b->type == JSON_STRING && !strcmp(a->valuestring, b->str) && strlen(b->str) == b->len;
  else if (cJSON_IsArray(a) || cJSON_IsObject(a)) {
    ok = b->type == (cJSON_IsArray(a) ? JSON_ARRAY : JSON_OBJECT) && cJSON_GetArraySize(a) == b->count;
    const cJSON *e = a->child;
    for (int i = 0; ok && i < b->count; i++, e = e->next) {
      const Json *f = &b->items[i];
      ok = e && (e->string == NULL) == (f->name == NULL) && (!e->string || !strcmp(e->string, f->name));
      if (e && e->string) snprintf(path + at, cap - at, ".%s", e->string);
      else snprintf(path + at, cap - at, "[%d]", i);
      if (ok) ok = same_tree(e, f, path, cap);
      if (ok) path[at] = 0;
    }
  } else ok = false;
  return ok;
}

static void crosscheck(const char *file, const cJSON *a, const char *text, size_t n) {
  Json *b = json_parse(text, n, NULL);
  char path[512] = "";
  if (!a != !b || (a && !same_tree(a, b, path, sizeof path))) {
    SDL_Log("assets: our JSON reader and cJSON disagree on %s at %s", file, path[0] ? path : "(the whole file)");
    abort();
  }
  json_free(b);
}
#else
#define crosscheck(file, a, text, n) ((void)0)
#endif

cJSON *assets_json(const char *rel) {
  const char *p = assets_path(rel);
  if (!p) return NULL;
  size_t n = 0;
  char *text = SDL_LoadFile(p, &n);
  if (!text) {
    SDL_Log("assets: cannot read %s", p);
    return NULL;
  }
  cJSON *j = cJSON_ParseWithLength(text, n);
  crosscheck(p, j, text, n);
  SDL_free(text);
  if (!j) SDL_Log("assets: %s is not valid JSON", p);
  return j;
}

unsigned assets_hex(const char *s) {
  if (!s) return 0;
  if (*s == '#') s++;
  return (unsigned)strtoul(s, NULL, 16);
}

bool assets_mesh(const cJSON *j, Mesh *m) {
  const cJSON *V = cJSON_GetObjectItemCaseSensitive(j, "v"), *F = cJSON_GetObjectItemCaseSensitive(j, "f");
  const cJSON *rad = cJSON_GetObjectItemCaseSensitive(j, "rad");
  if (!cJSON_IsArray(V) || !cJSON_IsArray(F)) return false;
  mesh_reset(m, cJSON_IsNumber(rad) ? (float)rad->valuedouble : 10);
  const cJSON *v;
  cJSON_ArrayForEach(v, V) {
    if (cJSON_GetArraySize(v) < 3) return false;
    mesh_vert(m, (float)cJSON_GetArrayItem(v, 0)->valuedouble, (float)cJSON_GetArrayItem(v, 1)->valuedouble,
              (float)cJSON_GetArrayItem(v, 2)->valuedouble);
  }
  const cJSON *f;
  cJSON_ArrayForEach(f, F) {
    const cJSON *idx = cJSON_GetArrayItem(f, 0), *col = cJSON_GetArrayItem(f, 1);
    int ii[FACE_MAX_V], n = 0;
    const cJSON *k;
    cJSON_ArrayForEach(k, idx) {
      if (n < FACE_MAX_V) ii[n] = k->valueint;
      n++;
    }
    if (n <= FACE_MAX_V) mesh_face(m, ii, n, assets_hex(cJSON_GetStringValue(col)));
  }
  return m->nf > 0;
}

static const char *str(const cJSON *o, const char *k) {
  const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, k));
  return s ? s : "";
}
static float num(const cJSON *o, const char *k, float def) {
  const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsNumber(n) ? (float)n->valuedouble : def;
}

/* The game-format table in a .js file (const X = [...] or {...}). */
static cJSON *json_in_js(const char *rel, char open, char close) {
  const char *p = assets_path(rel);
  if (!p) return NULL;
  size_t n = 0;
  char *text = SDL_LoadFile(p, &n);
  if (!text) return NULL;
  char *a = strchr(text, open), *b = strrchr(text, close);
  cJSON *j = a && b && b > a ? cJSON_ParseWithLength(a, (size_t)(b - a + 1)) : NULL;
  if (a && b && b > a) crosscheck(p, j, a, (size_t)(b - a + 1));
  SDL_free(text);
  if (!j) SDL_Log("assets: cannot parse %s", p);
  return j;
}

/* Far LOD by vertex clustering, as the browser game's jets patch makes it:
 * snap vertices to a grid, average each cell, drop collapsed and repeated faces. */
static void decimate(const Mesh *src, Mesh *dst, float cell) {
  mesh_reset(dst, src->rad);
  int *map = malloc(sizeof(int) * (size_t)src->nv);
  int *key = malloc(sizeof(int) * 3 * (size_t)src->nv);
  float *acc = calloc((size_t)src->nv, 4 * sizeof(float));
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
 * plain digits and dashes, "about" for the tilde-like signs. */
static void plain(char *s) {
  static const struct { const char *u, *a; } MAP[] = {
    { "\xc3\x97", "x" }, { "\xc2\xb2", "2" }, { "\xe2\x89\x88", "ABOUT " }, { "~", "ABOUT " }, { "\xe2\x80\x93", "-" }, { "\xe2\x80\x94", "-" },
    { "\xe2\x88\x92", "-" }, { "\xe2\x80\x99", "'" }, { "\xc2\xb7", "-" },
  };
  for (size_t m = 0; m < sizeof MAP / sizeof MAP[0]; m++) {
    char *p;
    size_t lu = strlen(MAP[m].u), la = strlen(MAP[m].a);
    while ((p = strstr(s, MAP[m].u))) memmove(p + la, p + lu, strlen(p + lu) + 1), memcpy(p, MAP[m].a, la);
  }
}
static bool    have_spec[MAX_JETS];

const JetSpec *assets_jet_spec(int jet) { return jet >= 0 && jet < MAX_JETS && have_spec[jet] ? &specs[jet] : NULL; }

static void load_specs(void) {
  cJSON *root = assets_json("jets/jets_data.json");
  const cJSON *j;
  cJSON_ArrayForEach(j, root) {
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
    const cJSON *e;
    int n = 0;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "sensors")) {
      const char *t = cJSON_GetStringValue(e);
      if (t) n += snprintf(s->sensors + n, sizeof s->sensors - (size_t)n, "%s%s", n ? ", " : "", t);
      if (n >= (int)sizeof s->sensors) break;
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "weapons")) {
      if (s->nweapons >= 4) break;
      snprintf(s->weapons[s->nweapons++], sizeof s->weapons[0], "%d X %s",
               (int)cJSON_GetNumberValue(cJSON_GetArrayItem(e, 1)), cJSON_GetStringValue(cJSON_GetArrayItem(e, 0)));
    }
    s->crew = (int)num(j, "crew", 1);
    s->roll = (int)num(j, "roll", 200);
    s->imperial = !strcmp(str(j, "units"), "imperial");
    s->g_max = num(j, "g_max", 9);
    s->g_min = num(j, "g_min", -3);
    s->alpha_max = num(j, "alpha_max", 25);
    char *txt[] = { s->bloc, s->era, s->inspired, s->role, s->blurb, s->radar, s->rcs, s->gun, s->engines, s->sensors,
                    s->weapons[0], s->weapons[1], s->weapons[2], s->weapons[3] };
    for (size_t q = 0; q < sizeof txt / sizeof txt[0]; q++) plain(txt[q]);
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "perf");
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
    const cJSON *g = cJSON_GetObjectItemCaseSensitive(j, "game");
    static const char *const STAT[7] = { "speed", "turn", "climb", "range", "field", "firepower", "stealth" };
    for (int q = 0; q < 7; q++) s->stat[q] = num(g, STAT[q], 5);
    have_spec[k] = true;
  }
  cJSON_Delete(root);
}

static void load_jets(void) {
  cJSON *root = json_in_js("jets/jets_game.js", '[', ']');
  if (!root) return;
  static Mesh near, far; /* scratch, reused for every jet */
  for (size_t o = 0; o < sizeof JET_ORDER / sizeof JET_ORDER[0]; o++) {
    const cJSON *j;
    cJSON_ArrayForEach(j, root) {
      if (strcmp(str(j, "id"), JET_ORDER[o].id) != 0) continue;
      const cJSON *g = cJSON_GetObjectItemCaseSensitive(j, "game");
      if (!assets_mesh(cJSON_GetObjectItemCaseSensitive(j, "mesh"), &near)) break;
      float minz = 0;
      for (int i = 0; i < near.nv; i++) minz = fminf(minz, near.v[i].z);
      decimate(&near, &far, 1.1f);
      int k = jets_add(JET_ORDER[o].id, str(j, "designation"), str(j, "name"), JET_ORDER[o].role, num(g, "speed", 6),
                       num(g, "turn", 6), num(g, "climb", 6), num(g, "range", 6), -minz, &near, &far);
      const cJSON *fl = cJSON_GetObjectItemCaseSensitive(j, "flight");
      if (k >= 0) {
        JETS[k].g_max = num(fl, "g_max", 9);
        JETS[k].g_min = num(fl, "g_min", -3);
      }
    }
  }
  cJSON_Delete(root);
  load_specs();
  SDL_Log("assets: %d jets", NJETS);
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
  cJSON *root = assets_json("maps/maps_index.json");
  const cJSON *e;
  cJSON_ArrayForEach(e, root) {
    if (nmaps >= MAX_MAPS) break;
    snprintf(maps[nmaps].id, sizeof maps[0].id, "%s", str(e, "id"));
    snprintf(maps[nmaps].name, sizeof maps[0].name, "%s", str(e, "name"));
    for (char *c = maps[nmaps].name; *c; c++)
      if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    nmaps++;
  }
  cJSON_Delete(root);
}

static vec3 vec_of(const cJSON *a) {
  return v3((float)cJSON_GetArrayItem(a, 0)->valuedouble, (float)cJSON_GetArrayItem(a, 1)->valuedouble,
            (float)cJSON_GetArrayItem(a, 2)->valuedouble);
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
  cJSON *j = assets_json(rel);
  if (!j) return false;
  snprintf(m->id, sizeof m->id, "%s", maps[idx].id);
  snprintf(m->name, sizeof m->name, "%s", maps[idx].name);
  snprintf(m->blurb, sizeof m->blurb, "%s", str(j, "blurb"));

  const cJSON *hm = cJSON_GetObjectItemCaseSensitive(j, "heightmap");
  const cJSON *org = cJSON_GetObjectItemCaseSensitive(hm, "origin");
  m->origin_x = (float)cJSON_GetArrayItem(org, 0)->valuedouble;
  m->origin_z = (float)cJSON_GetArrayItem(org, 1)->valuedouble;
  m->spacing = num(hm, "spacing", 200);
  float scale = num(hm, "scale", 0.1f);
  snprintf(rel, sizeof rel, "maps/%s", str(hm, "file"));
  size_t n = 0;
  uint8_t *raw = SDL_LoadFile(assets_path(rel), &n);
  if (!raw || n < MAP_SAMPLES * MAP_SAMPLES * 2) {
    SDL_free(raw);
    cJSON_Delete(j);
    return false;
  }
  for (int i = 0; i < MAP_SAMPLES * MAP_SAMPLES; i++) m->h[i] = (float)(int16_t)(raw[2 * i] | raw[2 * i + 1] << 8) * scale;
  SDL_free(raw);
  snprintf(rel, sizeof rel, "maps/%s", str(cJSON_GetObjectItemCaseSensitive(j, "surface"), "file"));
  raw = SDL_LoadFile(assets_path(rel), &n);
  if (!raw || n < MAP_CELLS * MAP_CELLS) {
    SDL_free(raw);
    cJSON_Delete(j);
    return false;
  }
  memcpy(m->mat, raw, MAP_CELLS * MAP_CELLS);
  SDL_free(raw);

  const cJSON *e;
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "palette")) {
    int id = (int)num(e, "id", (float)m->npal);
    if (id < 0 || id >= MAP_MAX_PAL) continue;
    m->pal[id].color = assets_hex(str(e, "color"));
    m->pal[id].water = !strcmp(str(e, "kind"), "water");
    m->pal[id].jitter = num(e, "jitter", 0);
    if (id + 1 > m->npal) m->npal = id + 1;
  }
  const cJSON *wl = cJSON_GetObjectItemCaseSensitive(j, "water_level");
  m->sea = cJSON_IsNumber(wl);
  m->water_level = m->sea ? (float)wl->valuedouble : 40;
  m->water_sky_mix = num(j, "water_sky_mix", 0.25f);
  m->far_ground = assets_hex(str(j, "sky_far_ground"));

  const cJSON *tods = cJSON_GetObjectItemCaseSensitive(j, "time_of_day");
  for (int t = 0; t < TOD_COUNT; t++) {
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(tods, TOD_KEYS[t]);
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
    d->sun = v3_norm(vec_of(cJSON_GetObjectItemCaseSensitive(o, "sun")));
  }

  const cJSON *wx = cJSON_GetObjectItemCaseSensitive(j, "weather");
  const cJSON *pre = cJSON_GetObjectItemCaseSensitive(wx, "presets");
  m->default_weather = WX_SCATTERED;
  for (int w = 0; w < WX_COUNT; w++) {
    if (!strcmp(str(wx, "default"), WX_KEYS[w])) m->default_weather = w;
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(pre, WX_KEYS[w]);
    Weather *W = &m->weather[w];
    W->visibility_km = num(o, "visibility_km", 30);
    W->sun_factor = num(o, "sun_factor", 1);
    W->ambient_factor = num(o, "ambient_factor", 1);
    const cJSON *cl = cJSON_GetObjectItemCaseSensitive(o, "clouds");
    W->nclouds = cJSON_GetArraySize(cl);
    W->clouds = W->nclouds ? calloc((size_t)W->nclouds, sizeof(Cloud)) : NULL;
    int k = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, cl) {
      Cloud *C = &W->clouds[k++];
      C->x = num(c, "x", 0);
      C->z = num(c, "z", 0);
      C->base = num(c, "base", 1000);
      C->top = num(c, "top", 2000);
      const char *genus = str(c, "genus");
      C->albedo = (!strcmp(genus, "Cb") ? 0.92f : !strcmp(genus, "Fs") ? 0.62f : 1.0f) * (w == WX_STORM ? 0.72f : 1);
      const cJSON *pl = cJSON_GetObjectItemCaseSensitive(c, "puffs");
      C->npuffs = cJSON_GetArraySize(pl);
      C->puffs = calloc((size_t)(C->npuffs ? C->npuffs : 1), sizeof(Puff));
      int q = 0;
      const cJSON *pf;
      cJSON_ArrayForEach(pf, pl) {
        Puff *P = &C->puffs[q++];
        P->dx = (float)cJSON_GetArrayItem(pf, 0)->valuedouble;
        P->dy = (float)cJSON_GetArrayItem(pf, 1)->valuedouble;
        P->dz = (float)cJSON_GetArrayItem(pf, 2)->valuedouble;
        P->r = (float)cJSON_GetArrayItem(pf, 3)->valuedouble;
        P->tier = cJSON_GetArraySize(pf) > 4 ? cJSON_GetArrayItem(pf, 4)->valueint : 0;
      }
    }
  }

  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "bases")) {
    int t = (int)num(e, "team", 0) & 1;
    m->bases[t].x = num(e, "x", 0);
    m->bases[t].z = num(e, "z", 0);
    m->bases[t].elev = num(e, "elev", 0);
    m->bases[t].heading = num(e, "heading", 0);
  }
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "carriers")) {
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
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "targets")) {
    int type = target_type(str(e, "type"));
    if (type < 0 || m->ntargets >= MAP_MAX_TGT) continue;
    MapTarget *t = &m->targets[m->ntargets++];
    t->team = (int)num(e, "team", 0);
    t->type = type;
    t->x = num(e, "x", 0);
    t->z = num(e, "z", 0);
    t->heading = num(e, "heading", 0);
  }
  bool got[2] = { false, false };
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "spawns")) {
    int t = (int)num(e, "team", 0) & 1;
    if (got[t] || strcmp(str(e, "kind"), "air") != 0) continue;
    got[t] = true;
    m->spawn[t].x = num(e, "x", 0);
    m->spawn[t].y = num(e, "y", 1500);
    m->spawn[t].z = num(e, "z", 0);
    m->spawn[t].heading = num(e, "heading", 0);
  }
  const cJSON *tk = cJSON_GetObjectItemCaseSensitive(j, "tanker");
  m->tanker_x = num(tk, "x", 0);
  m->tanker_z = num(tk, "z", 0);
  m->tanker_r = num(tk, "radius", 3200);
  m->tanker_alt = num(tk, "alt", 2300);

  const cJSON *lc = cJSON_GetObjectItemCaseSensitive(j, "light_colors");
  const cJSON *ls = cJSON_GetObjectItemCaseSensitive(j, "lights");
  m->nlights = cJSON_GetArraySize(ls);
  m->lights = calloc((size_t)(m->nlights ? m->nlights : 1), sizeof(MapLight));
  int k = 0;
  cJSON_ArrayForEach(e, ls) {
    MapLight *L = &m->lights[k++];
    L->x = num(e, "x", 0);
    L->y = num(e, "y", 0);
    L->z = num(e, "z", 0);
    L->size = num(e, "size", 1);
    L->blink = num(e, "blink", 0);
    L->color = assets_hex(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(lc, str(e, "kind"))));
    if (!L->color) L->color = 0xffffff;
  }
  cJSON_Delete(j);
  SDL_Log("map %s: %d targets, %d carriers, %d lights", m->id, m->ntargets, m->ncarriers, m->nlights);
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
  cJSON *root = json_in_js("jets/carrier_game.js", '{', '}');
  if (root && assets_mesh(cJSON_GetObjectItemCaseSensitive(root, "mesh"), &MESH_CARRIER))
    SDL_Log("assets: carrier, %d faces", MESH_CARRIER.nf);
  cJSON_Delete(root);
}

bool assets_init(void) {
  const char *base = SDL_GetBasePath();
  char cand[3][1024];
  int n = 0;
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
    SDL_Log("assets: no assets folder found; flying the built-in jet only");
    return false;
  }
  SDL_Log("assets: %s", dir);
  load_jets();
  load_carrier();
  load_map_index();
  return true;
}
