/* render3d.c - camera, near clip, sky and ground, terrain, meshes, clouds
 * and effects for one view (JS toCam, clipNear, addMesh, drawSkyGround,
 * drawGrid, drawFlats, renderView and render; lines 3282-3485 and
 * 3662-3680), plus the maps of assets/maps/FORMAT.md.
 *
 * Opaque geometry (terrain, runways, every mesh face) is drawn as it comes
 * with a depth buffer of 1/z, so draw order no longer matters and the
 * terrain hides what is behind hills. Tracers are depth-tested lines. Things
 * with alpha (particles, smoke, cloud puffs, night lights) are collected,
 * sorted far to near as the JS painter's list was, and drawn last. */
#include "render3d.h"
#include "viewfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/defend.h"
#include "command.h"
#include "assets.h"
#include "gpu.h"
#include "hud.h"
#include "stores.h"
#include "surfaces.h"

/* the near plane: 1 m for jets, much closer on foot so walls at arm's length still show */
static float near_z = NEAR_Z;
void r3d_near(float z) { near_z = z; }

/* ---- colours ---------------------------------------------------------- */
#define SKY_TOP     0x3d5cc0
#define SKY_HOR     0xb6cce6
#define GROUND_FAR  0xa7bba4
#define GROUND_MID  0x6b984b
#define GROUND_NEAR 0x557d38
#define CLASSIC_FOG 0xb6cce4

static uint32_t mix(uint32_t a, uint32_t b, float t) {
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  return rgb((uint8_t)(ar + (br - ar) * t), (uint8_t)(ag + (bg - ag) * t), (uint8_t)(ab + (bb - ab) * t));
}
static inline float ch(uint32_t c, int s) { return (float)((c >> s) & 255); }
static inline uint8_t sat(float v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* Lighting set for the current view: the classic JS look, or a map's time
 * of day and weather. */
typedef struct {
  bool     map;
  uint32_t sky_top, sky_hor, fog, ground_far;
  float    fog_start, fog_range, fog_max;
  float    amb[3], sun[3];  /* light colours, 0..~1.5 */
  float    stars, lights;
  uint32_t sun_disc;
} Look;
static Look L;

static void set_look(void) {
  memset(&L, 0, sizeof L);
  if (!MAP) {
    L.sky_top = SKY_TOP;
    L.sky_hor = SKY_HOR;
    L.fog = CLASSIC_FOG;
    L.fog_start = 1500;
    L.fog_range = 16000;
    L.fog_max = 0.8f;
    return;
  }
  const TimeOfDay *t = &MAP->tod[MAP_TOD];
  const Weather *w = &MAP->weather[MAP_WX];
  L.map = true;
  L.sky_top = t->sky_top;
  L.sky_hor = t->sky_hor;
  L.fog = t->fog;
  L.ground_far = MAP->far_ground;
  float vis = w->visibility_km * 1000 * 1.2f / (t->fog_density > 0.05f ? t->fog_density : 0.05f);
  L.fog_start = 300;
  L.fog_range = fminf(vis, FAR_Z);
  /* the far ground and sea never fade all the way into the haze: where the haze's colour is the sky's own (a
   * clear day), the sea would melt into the sky and the horizon seem lower than the level line the HUD draws.
   * Clear skies keep 40 % of the ground's colour at the horizon, overcast and storm 28 %. */
  L.fog_max = MAP_WX == WX_CLEAR || MAP_WX == WX_SCATTERED ? 0.6f : 0.72f;
  for (int i = 0; i < 3; i++) {
    int s = 16 - 8 * i;
    L.amb[i] = ch(t->ambient, s) / 255 * t->ambient_intensity * w->ambient_factor;
    L.sun[i] = ch(t->sun_color, s) / 255 * t->sun_intensity * w->sun_factor;
  }
  L.stars = t->stars;
  L.lights = t->lights;
  L.sun_disc = t->sun_color;
}

void render_fog(float *start, float *range, float *max) {
  set_look(); /* the look of the map, time and weather set now */
  *start = L.fog_start, *range = L.fog_range, *max = L.fog_max;
}

/* The opening film's look (r3d_film_look): its dawn sea has no far ground to show, and its clouds are seen close */
static bool film_look;
void r3d_film_look(bool on) { film_look = on; }

static float fog_at(float d) { return clampf((d - L.fog_start) / L.fog_range, 0, L.fog_max); }

/* Face colour: the JS flat shade (0.5 + 0.5 |n.light|) for the classic
 * arena; ambient plus sun light for a map. Then distance fog. */
static uint32_t shade_face(uint32_t col, vec3 n, float fogT) {
  float c[3] = { ch(col, 16), ch(col, 8), ch(col, 0) }, o[3];
  if (!L.map) {
    float lit = 0.5f + 0.5f * fabsf(v3_dot(n, LIGHT));
    for (int i = 0; i < 3; i++) o[i] = c[i] * lit;
  } else {
    float k = fabsf(v3_dot(n, LIGHT)); /* two-sided, as the JS */
    for (int i = 0; i < 3; i++) o[i] = c[i] * (L.amb[i] * 0.85f + L.sun[i] * (0.25f + 0.75f * k));
  }
  for (int i = 0; i < 3; i++) o[i] = o[i] * (1 - fogT) + ch(L.fog, 16 - 8 * i) * fogT;
  return rgb(sat(o[0]), sat(o[1]), sat(o[2]));
}

static uint32_t fogged(uint32_t c, float fogT) { return mix(c, L.fog, fogT); }

/* ---- the alpha list ---------------------------------------------------- */
enum { IT_DOT, IT_DISC };
typedef struct {
  float z, x, y, s, a;
  uint32_t c;
  uint8_t kind;
} Item;

#define MAX_ITEMS 24000
static Item items[MAX_ITEMS];
static int  nitems;

static Item *new_item(float z, int kind) {
  if (nitems >= MAX_ITEMS) return NULL;
  Item *it = &items[nitems++];
  it->z = z;
  it->kind = (uint8_t)kind;
  return it;
}

static int cmp_items(const void *a, const void *b) {
  float za = ((const Item *)a)->z, zb = ((const Item *)b)->z;
  return za < zb ? 1 : za > zb ? -1 : 0;
}

static void draw_list(Framebuffer *fb) {
  qsort(items, (size_t)nitems, sizeof items[0], cmp_items);
  for (int i = 0; i < nitems; i++) {
    const Item *it = &items[i];
    float w = 1 / it->z;
    if (it->kind == IT_DISC) {
      /* a big one (a cloud puff or smoke close by, or through a long lens) has the GPU sprite's soft edge: as a
       * hard disc it reads as a flat circle over the picture, not as cloud */
      if (it->s > fb->h * 0.12f) fb_disc_soft_z(fb, it->x, it->y, it->s, it->c, it->a, w);
      else fb_disc_z(fb, it->x, it->y, it->s, it->c, it->a, w);
    } else {
      float s = it->s;
      int sz = (int)(s + 0.5f);
      if (sz < 1) sz = 1;
      fb_rect_z(fb, (int)floorf(it->x - s / 2 + 0.5f), (int)floorf(it->y - s / 2 + 0.5f), sz, sz, it->c, it->a, w);
    }
  }
  nitems = 0;
}

/* ---- projection and clipping ------------------------------------------ */
static vec3 cut_near(vec3 a, vec3 b) {
  float t = (near_z - a.z) / (b.z - a.z);
  return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, near_z);
}

static int clip_near(const vec3 *in, int n, vec3 *out) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    vec3 a = in[i], b = in[(i + 1) % n];
    bool ain = a.z >= near_z, bin = b.z >= near_z;
    if (ain) out[m++] = a;
    if (ain != bin) out[m++] = ain ? cut_near(a, b) : cut_near(b, a);
  }
  return m;
}

static inline float sx_of(const View *V, vec3 c) { return V->cx + c.x / c.z * V->F; }
static inline float sy_of(const View *V, vec3 c) { return V->cy - c.y / c.z * V->F; }

/* A camera-space polygon, clipped at the near plane, depth-tested. */
static void poly_cam(Framebuffer *fb, const View *V, const vec3 *pts, int n, uint32_t col, float grow, float bias) {
  vec3 clipped[FACE_MAX_V * 2];
  int behind = 0;
  for (int i = 0; i < n; i++) behind += pts[i].z < near_z;
  if (behind == n) return;
  if (behind) {
    n = clip_near(pts, n, clipped);
    pts = clipped;
  }
  if (n < 3) return;
  float xy[FACE_MAX_V * 4], w[FACE_MAX_V * 2];
  for (int i = 0; i < n; i++) {
    xy[2 * i] = sx_of(V, pts[i]);
    xy[2 * i + 1] = sy_of(V, pts[i]);
    w[i] = 1 / pts[i].z;
  }
  fb_poly_z(fb, xy, w, n, col, 1, grow, 1, bias);
}

static bool inside_bridge, hide_captain;

/* On the GPU renderer everything below is recorded for gpu.c instead of
 * drawn; these say how the next sprites and lines should look there. */
static int   spr_kind = SPR_SOFT;
static float line_glow = 1, gloss_override = -1;
void r3d_sprite_kind(int kind) { spr_kind = kind; }
void r3d_gloss(float g) { gloss_override = g; }

static float mesh_gloss(const Mesh *M) {
  if (gloss_override >= 0) return gloss_override;
  if (M == &MESH_TANKER || M == &MESH_JET[0] || M == &MESH_JET[1]) return 1;
  for (int j = 0; j < MAX_JETS; j++)
    for (int t = 0; t < 2; t++)
      if (M == &JETS[j].mesh[t] || M == &JETS[j].lod[t] || M == &JETS[j].hi[t]) return 1;
  if (M == &MESH_CARRIER || M == &MESH_TARGET[0][TGT_SHIP] || M == &MESH_CIWS_GUN) return 0.25f;
  return 0;
}
void r3d_inside_bridge(bool on) { inside_bridge = on; }
void r3d_hide_captain(bool on) { hide_captain = on; }

/* Faces of the carrier's island (every corner inside the island block). */
static bool island_face(const Mesh *M, const Face *fc) {
  for (int i = 0; i < fc->n; i++) {
    vec3 v = M->v[fc->i[i]];
    if (v.x < 23 || v.x > 34.5f || v.z < -59.5f || v.z > -24.5f || v.y < 21) return false;
  }
  return true;
}

/* scratch for transformed vertices, grown to the largest mesh drawn */
static vec3 *scratch;
static int   nscratch;

static void add_mesh_dim(Framebuffer *fb, const View *V, const Mesh *M, vec3 pos, vec3 r, vec3 u, vec3 f, float s,
                         uint32_t dotCol, bool hasDot, float dim) {
  if (!M->nv) return;
  vec3 cc = to_cam(&V->cam, pos);
  float rad = M->rad * s;
  if (cc.z < -rad) return;
  float dl = v3_len(cc);
  if (dl > FAR_Z + rad) return;
  if (cc.z > rad) {
    if (fabsf(cc.x) > V->vw / 2 / V->F * cc.z + rad * 2) return;
    if (fabsf(cc.y) > V->sh / 2 / V->F * cc.z + rad * 2) return;
    if (rad / cc.z * V->F < 1.6f) {
      if (hasDot) fb_rect_z(fb, (int)floorf(sx_of(V, cc) - 0.5f), (int)floorf(sy_of(V, cc) - 0.5f), 2, 2, hexc(dotCol), 1, 1 / cc.z);
      return;
    }
  }
  if (M->lod && rad / fmaxf(cc.z, 1) * V->F < 45) M = M->lod;
  else if (gpu_ultra() && rad / fmaxf(cc.z, 1) * V->F >= 45) {
    static bool hi_ready;
    if (!hi_ready) {
      hi_ready = true;
      assets_load_hi_jets();
      for (int j = 0; j < NJETS; j++) stores_set_hi(j);
    }
    if (M->hi && M->hi->nv) M = M->hi;
  }
  if (gpu_active()) {
    gpu_mesh(M, pos, r, u, f, s, dim, mesh_gloss(M), inside_bridge && M == &MESH_CARRIER);
    return;
  }
  if (M->nv * 2 > nscratch) {
    vec3 *p = realloc(scratch, (size_t)M->nv * 2 * sizeof *p);
    if (!p) return;
    scratch = p;
    nscratch = M->nv * 2;
  }
  vec3 *wv = scratch, *cv = scratch + M->nv;
  for (int i = 0; i < M->nv; i++) {
    vec3 v = M->v[i];
    wv[i] = v3_add(pos, v3_scale(v3_add(v3_add(v3_scale(r, v.x), v3_scale(u, v.y)), v3_scale(f, v.z)), s));
    cv[i] = to_cam(&V->cam, wv[i]);
  }
  float fogT = fog_at(dl);
  bool cull = inside_bridge && M == &MESH_CARRIER;
  for (int k = 0; k < M->nf; k++) {
    const Face *fc = &M->f[k];
    if (cull && island_face(M, fc)) continue;
    vec3 pts[FACE_MAX_V];
    for (int i = 0; i < fc->n; i++) pts[i] = cv[fc->i[i]];
    vec3 a = wv[fc->i[0]];
    vec3 nrm = v3_norm(v3_cross(v3_sub(wv[fc->i[1]], a), v3_sub(wv[fc->i[2]], a)));
    uint32_t col = fc->c & 0xffffff;
    if (fc->c & FACE_GLOW) {
      poly_cam(fb, V, pts, fc->n, fogged(col, fogT), 0.3f, 1);
      continue;
    }
    if (dim != 1) col = (uint32_t)(sat(ch(col, 16) * dim) << 16 | sat(ch(col, 8) * dim) << 8 | sat(ch(col, 0) * dim));
    poly_cam(fb, V, pts, fc->n, shade_face(col, nrm, fogT), 0.3f, 1);
  }
}

static void add_mesh(Framebuffer *fb, const View *V, const Mesh *M, vec3 pos, vec3 r, vec3 u, vec3 f, float s,
                     uint32_t dotCol, bool hasDot) {
  add_mesh_dim(fb, V, M, pos, r, u, f, s, dotCol, hasDot, 1);
}

static void add_dot(const View *V, vec3 w, float sizeW, uint32_t col, float a, float minPx) {
  if (gpu_active()) {
    gpu_sprite(w, sizeW * 0.5f, minPx, col & 0xffffff, a, spr_kind);
    return;
  }
  vec3 c = to_cam(&V->cam, w);
  if (c.z < near_z || c.z > FAR_Z) return;
  float s = fminf(V->sh * 0.12f, fmaxf(minPx, sizeW / c.z * V->F));
  float x = sx_of(V, c), y = sy_of(V, c);
  if (x < V->vx - s || x > V->vx + V->vw + s || y < V->vy - s || y > V->vy + V->sh + s) return;
  Item *it = new_item(c.z, IT_DOT);
  if (!it) return;
  it->x = x;
  it->y = y;
  it->s = s;
  it->c = L.map ? fogged(col, fog_at(c.z) * 0.7f) : col;
  it->a = a;
}

static void draw_line(Framebuffer *fb, const View *V, vec3 a, vec3 b, uint32_t col, float w) {
  if (gpu_active()) {
    gpu_line(a, b, col & 0xffffff, 1, line_glow);
    return;
  }
  vec3 ca = to_cam(&V->cam, a), cb = to_cam(&V->cam, b);
  if (ca.z < near_z && cb.z < near_z) return;
  if (ca.z < near_z) ca = cut_near(cb, ca);
  else if (cb.z < near_z) cb = cut_near(ca, cb);
  fb_line_z(fb, sx_of(V, ca), sy_of(V, ca), sx_of(V, cb), sy_of(V, cb), col, w, 1, 2 / (ca.z + cb.z) * 1.002f);
}

/* ---- sky, ground, grid, runways --------------------------------------- */
static void draw_sky_ground(Framebuffer *fb, const View *V) {
  const Camera *cam = &V->cam;
  float gx = cam->r.y, gy = -cam->u.y, g0 = cam->f.y * V->F, gl2 = gx * gx + gy * gy;
  float hx = V->cx, hy = V->cy, nx = 0, ny = -1;
  if (gl2 > 1e-6f) {
    float gl = sqrtf(gl2);
    nx = gx / gl;
    ny = gy / gl;
    hx = V->cx - nx * g0 / gl;
    hy = V->cy - ny * g0 / gl;
  }
  /* Gradient lookup tables: t from 0 at the horizon to 1. */
  enum { LUT = 256 };
  uint32_t sky[LUT], gnd[LUT];
  for (int i = 0; i < LUT; i++) {
    float t = (float)i / (LUT - 1);
    sky[i] = mix(L.sky_hor, L.sky_top, t);
    if (L.map) gnd[i] = mix(L.fog, L.ground_far, fminf(1, 1 - L.fog_max + t * 2.5f)); /* from the fog's limit */
    else gnd[i] = t < 0.18f ? mix(GROUND_FAR, GROUND_MID, t / 0.18f) : mix(GROUND_MID, GROUND_NEAR, (t - 0.18f) / 0.82f);
  }
  int x0 = (int)V->vx, x1 = (int)(V->vx + V->vw), y0 = (int)V->vy, y1 = (int)(V->vy + V->sh);
  if (x0 < fb->cx0) x0 = fb->cx0;
  if (y0 < fb->cy0) y0 = fb->cy0;
  if (x1 > fb->cx1) x1 = fb->cx1;
  if (y1 > fb->cy1) y1 = fb->cy1;
  float ks = (LUT - 1) / V->sh, kg = (LUT - 1) / (V->sh * 0.9f);
  for (int y = y0; y < y1; y++) {
    uint32_t *row = fb->px + (size_t)y * fb->w;
    float py = y + 0.5f;
    float v = gx * (x0 + 0.5f - V->cx) + gy * (py - V->cy) + g0;
    float t = (x0 + 0.5f - hx) * nx + (py - hy) * ny;
    for (int x = x0; x < x1; x++, v += gx, t += nx) {
      if (v < 0) {
        float k = -t * kg;
        int i = k > 0 ? (k < LUT - 1 ? (int)k : LUT - 1) : 0; /* NaN (a broken view) fails k > 0: row 0 */
        row[x] = gnd[i];
      } else {
        float k = t * ks;
        int i = k > 0 ? (k < LUT - 1 ? (int)k : LUT - 1) : 0; /* NaN (a broken view) fails k > 0: row 0 */
        row[x] = sky[i];
      }
    }
  }
}

/* Stars and the sun or moon disc, drawn into the sky before the terrain. */
static void draw_sky_objects(Framebuffer *fb, const View *V, float time) {
  const Camera *cam = &V->cam;
  vec3 s = v3(v3_dot(LIGHT, cam->r), v3_dot(LIGHT, cam->u), v3_dot(LIGHT, cam->f));
  if (s.z > 0.05f && LIGHT.y > -0.05f) {
    float x = V->cx + s.x / s.z * V->F, y = V->cy - s.y / s.z * V->F, r = fmaxf(2, V->F * 0.02f);
    /* at a depth just off infinity: the cleared depth buffer holds 0 and only nearer pixels pass, so at 0 the
     * disc was never drawn; anything in the world (1/z far above 1e-9) still covers it */
    fb_disc_z(fb, x, y, r * 2.2f, rgb(sat(ch(L.sun_disc, 16)), sat(ch(L.sun_disc, 8)), sat(ch(L.sun_disc, 0))), 0.25f, 1e-9f);
    fb_disc_z(fb, x, y, r, mix(L.sun_disc, 0xffffff, 0.6f), 1, 1e-9f);
  }
  if (L.stars <= 0.01f) return;
  Rng rng = { 4242u };
  for (int i = 0; i < 400; i++) {
    float az = (float)rng_f(&rng) * 6.2831853f, el = asinf((float)rng_f(&rng));
    float b = (float)rng_f(&rng);
    if (el < 0.06f) continue;
    vec3 d = v3(cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az));
    vec3 c = v3(v3_dot(d, cam->r), v3_dot(d, cam->u), v3_dot(d, cam->f));
    if (c.z < 0.05f) continue;
    float tw = 0.75f + 0.25f * sinf(time * 3 + i);
    uint8_t v = sat(140 + 115 * b * tw);
    fb_rect_a(fb, (int)(V->cx + c.x / c.z * V->F), (int)(V->cy - c.y / c.z * V->F), 1, 1, rgb(v, v, (uint8_t)(v > 235 ? 255 : v + 20)),
              L.stars * (0.4f + 0.6f * b));
  }
}

static void draw_grid(Framebuffer *fb, const View *V) {
  const Camera *cam = &V->cam;
  const float S = 600;
  const int N = 12;
  if (cam->pos.y > 9000) return;
  float bx = roundf(cam->pos.x / S) * S, bz = roundf(cam->pos.z / S) * S;
  static const float alpha[3] = { 0.5f, 0.3f, 0.14f };
  const uint32_t col = rgb(28, 58, 18);
  for (int i = -N; i <= N; i++)
    for (int j = -N; j < N; j++)
      for (int dir = 0; dir < 2; dir++) {
        float ax, az, bx2, bz2;
        if (!dir) { ax = bx + i * S; az = bz + j * S; bx2 = ax; bz2 = bz + (j + 1) * S; }
        else { ax = bx + j * S; az = bz + i * S; bx2 = bx + (j + 1) * S; bz2 = az; }
        float mx = (ax + bx2) / 2 - cam->pos.x, mz = (az + bz2) / 2 - cam->pos.z;
        float md = sqrtf(mx * mx + mz * mz);
        if (md > 7600) continue;
        vec3 a = to_cam(cam, v3(ax, 0, az)), b = to_cam(cam, v3(bx2, 0, bz2));
        if (a.z < near_z && b.z < near_z) continue;
        if (a.z < near_z) a = cut_near(b, a);
        else if (b.z < near_z) b = cut_near(a, b);
        fb_line(fb, sx_of(V, a), sy_of(V, a), sx_of(V, b), sy_of(V, b), col, 1, alpha[md < 2600 ? 0 : md < 5200 ? 1 : 2]);
      }
}

static void quad_xz(Framebuffer *fb, const View *V, float x0, float z0, float x1, float z1, float y, uint32_t col, float bias) {
  if (gpu_active()) {
    float up = (bias - 1) * 300; /* the decal order the software bias gave */
    gpu_quad(v3(x0, y + up, z0), v3(x1, y + up, z0), v3(x1, y + up, z1), v3(x0, y + up, z1), col);
    return;
  }
  vec3 q[4] = { to_cam(&V->cam, v3(x0, y, z0)), to_cam(&V->cam, v3(x1, y, z0)), to_cam(&V->cam, v3(x1, y, z1)),
                to_cam(&V->cam, v3(x0, y, z1)) };
  float d = v3_dist(v3((x0 + x1) / 2, y, (z0 + z1) / 2), V->cam.pos);
  poly_cam(fb, V, q, 4, L.map ? fogged(col, fog_at(d)) : hexc(col), 0, bias);
}

static void draw_flats(Framebuffer *fb, const View *V) {
  for (int t = 0; t < 2; t++) {
    const Base *b = &BASES[t];
    float y = MAP ? MAP->bases[t].elev + 0.3f : 0.3f;
    if (v3_dist(v3(b->x, y, b->z), V->cam.pos) > 16000) continue;
    /* decals: pulled toward the camera so they win over the ground under them */
    /* a real fighter-base runway: 2000 x 45 m with ICAO markings - edge
     * lines, a dashed centre line (36 m stripes, 24 m gaps), piano keys at
     * each threshold and the aiming-point bars 400 m in */
    const float HW = 22.5f, HL = 1000, W = 0.9f;
    if (!MAP) quad_xz(fb, V, b->x - 150, b->z - HL - 60, b->x + 150, b->z + HL + 60, y, t ? 0x7d8a5a : 0x6a8a6a, 1.0005f);
    quad_xz(fb, V, b->x - HW - 7.5f, b->z - HL - 60, b->x + HW + 7.5f, b->z + HL + 60, y, 0x6e6a62, 1.0008f); /* shoulders and blast pads */
    quad_xz(fb, V, b->x - HW, b->z - HL, b->x + HW, b->z + HL, y, 0x4a4d52, 1.001f);
    for (int s = -1; s <= 1; s += 2) quad_xz(fb, V, b->x + s * (HW - 1.5f) - W / 2, b->z - HL, b->x + s * (HW - 1.5f) + W / 2, b->z + HL, y, 0xe8e8e0, 1.002f);
    for (float z = -HL + 150; z < HL - 186; z += 60) quad_xz(fb, V, b->x - W / 2, b->z + z, b->x + W / 2, b->z + z + 36, y, 0xe8e8e0, 1.002f);
    for (int s = -1; s <= 1; s += 2) {
      float z0 = b->z + s * (HL - 6), z1 = b->z + s * (HL - 51);
      for (int k = 0; k < 4; k++) { /* piano keys, four each side of the centre line */
        float x = 3 + k * 4.5f;
        quad_xz(fb, V, b->x + x, fminf(z0, z1), b->x + x + 1.8f, fmaxf(z0, z1), y, 0xe8e8e0, 1.002f);
        quad_xz(fb, V, b->x - x - 1.8f, fminf(z0, z1), b->x - x, fmaxf(z0, z1), y, 0xe8e8e0, 1.002f);
      }
      float a0 = b->z + s * (HL - 400), a1 = b->z + s * (HL - 460);
      for (int q = -1; q <= 1; q += 2) quad_xz(fb, V, b->x + q * 6 - 5, fminf(a0, a1), b->x + q * 6 + 5, fmaxf(a0, a1), y, 0xe8e8e0, 1.002f);
      /* the team's colour across the end of the runway */
      quad_xz(fb, V, b->x - HW, fminf(b->z + s * (HL + 2), b->z + s * (HL + 8)), b->x + HW, fmaxf(b->z + s * (HL + 2), b->z + s * (HL + 8)), y,
              TEAM[t].bld, 1.002f);
    }
  }
}

/* ---- map terrain ------------------------------------------------------ */
/* Triangle colours for the 200, 400 and 800 m grids, lit for the map's time
 * of day and weather (FORMAT.md "Palette and lighting"). */
typedef struct {
  const Map *map;
  int tod, wx;
  uint32_t *col[3]; /* [cell * 2 + t] */
} TerrainCache;
static TerrainCache TC;

static float hs(const Map *m, int i, int j) {
  if (i > MAP_SAMPLES - 1) i = MAP_SAMPLES - 1;
  if (j > MAP_SAMPLES - 1) j = MAP_SAMPLES - 1;
  return m->h[j * MAP_SAMPLES + i];
}

static float hash01(uint32_t i, uint32_t j, uint32_t t) {
  uint32_t n = (i * 73856093u) ^ (j * 19349663u) ^ (t * 83492791u);
  n = (n ^ (n >> 13)) * 1274126177u;
  n ^= n >> 16;
  return (float)(n & 255) / 255;
}

static void terrain_cache(void) {
  if (TC.map == MAP && TC.tod == MAP_TOD && TC.wx == MAP_WX) return;
  TC.map = MAP;
  TC.tod = MAP_TOD;
  TC.wx = MAP_WX;
  const Map *m = MAP;
  const TimeOfDay *t = &m->tod[MAP_TOD];
  float avg[3];
  for (int c = 0; c < 3; c++) avg[c] = (ch(t->sky_hor, 16 - 8 * c) + ch(t->sky_top, 16 - 8 * c)) / 2;
  for (int lv = 0; lv < 3; lv++) {
    int s = 1 << lv, n = MAP_CELLS / s;
    free(TC.col[lv]);
    TC.col[lv] = malloc(sizeof(uint32_t) * (size_t)n * n * 2);
    for (int j = 0; j < n; j++)
      for (int i = 0; i < n; i++) {
        int I = i * s, J = j * s;
        vec3 a = v3(0, hs(m, I, J), 0), b = v3((float)s * m->spacing, hs(m, I + s, J), 0);
        vec3 c = v3((float)s * m->spacing, hs(m, I + s, J + s), (float)s * m->spacing), d = v3(0, hs(m, I, J + s), (float)s * m->spacing);
        int mi = m->mat[(J + s / 2) * MAP_CELLS + I + s / 2];
        if (mi >= m->npal) mi = 0;
        for (int tri = 0; tri < 2; tri++) {
          vec3 n0 = tri ? v3_cross(v3_sub(d, a), v3_sub(c, a)) : v3_cross(v3_sub(c, a), v3_sub(b, a));
          n0 = v3_norm(n0);
          if (n0.y < 0) n0 = v3_scale(n0, -1);
          float sunk = fmaxf(0, v3_dot(n0, t->sun.y > -0.2f ? v3_norm(t->sun) : v3(0, 1, 0)));
          float jit = 1 + m->pal[mi].jitter * (2 * hash01((uint32_t)i, (uint32_t)j, (uint32_t)tri) - 1);
          float o[3];
          for (int k = 0; k < 3; k++) {
            float shade = L.amb[k] + L.sun[k] * sunk;
            o[k] = ch(m->pal[mi].color, 16 - 8 * k) * shade * jit;
            if (m->pal[mi].water) {
              /* water mostly shows the sky, so the light only half-darkens it */
              float tint = ch(t->water_tint, 16 - 8 * k) / 255;
              o[k] = ch(m->pal[mi].color, 16 - 8 * k) * fminf(1.15f, 0.5f + 0.5f * shade) * jit * (0.55f + 0.45f * tint) *
                       (1 - m->water_sky_mix) + avg[k] * m->water_sky_mix;
            }
          }
          TC.col[lv][(j * n + i) * 2 + tri] = rgb(sat(o[0]), sat(o[1]), sat(o[2]));
        }
      }
  }
}

/* The heightmap as flat-shaded triangles, finer near the camera: 4x4-cell
 * blocks pick a 200, 400 or 800 m grid by distance. */
static void draw_terrain(Framebuffer *fb, const View *V) {
  const Map *m = MAP;
  terrain_cache();
  const Camera *cam = &V->cam;
  const float bs = 4 * m->spacing; /* block size */
  for (int bj = 0; bj < MAP_CELLS / 4; bj++)
    for (int bi = 0; bi < MAP_CELLS / 4; bi++) {
      float bx = m->origin_x + (bi + 0.5f) * bs, bz = m->origin_z + (bj + 0.5f) * bs;
      float hmin = 1e9f, hmax = -1e9f;
      for (int q = 0; q <= 4; q += 4)
        for (int p = 0; p <= 4; p += 4) {
          float h = hs(m, bi * 4 + p, bj * 4 + q);
          hmin = fminf(hmin, h);
          hmax = fmaxf(hmax, h);
        }
      vec3 c = to_cam(cam, v3(bx, (hmin + hmax) / 2, bz));
      float rad = bs * 0.75f + (hmax - hmin) + 400;
      if (c.z < -rad) continue;
      float d = v3_len(c);
      if (d > FAR_Z + rad) continue;
      if (c.z > rad) {
        if (fabsf(c.x) > V->vw / 2 / V->F * c.z + rad * 1.5f) continue;
        if (fabsf(c.y) > V->sh / 2 / V->F * c.z + rad * 1.5f) continue;
      }
      int lv = d < 2600 ? 0 : d < 7000 ? 1 : 2, s = 1 << lv, n = 4 / s, N = MAP_CELLS / s;
      vec3 g[5][5];
      for (int q = 0; q <= n; q++)
        for (int p = 0; p <= n; p++) {
          int I = bi * 4 + p * s, J = bj * 4 + q * s;
          g[q][p] = to_cam(cam, v3(m->origin_x + I * m->spacing, hs(m, I, J), m->origin_z + J * m->spacing));
        }
      for (int q = 0; q < n; q++)
        for (int p = 0; p < n; p++) {
          int ci = (bi * 4) / s + p, cj = (bj * 4) / s + q;
          vec3 a = g[q][p], b = g[q][p + 1], cc = g[q + 1][p + 1], dd = g[q + 1][p];
          float fd = v3_len(v3_scale(v3_add(a, cc), 0.5f));
          float fogT = fog_at(fd);
          vec3 t0[3] = { a, b, cc }, t1[3] = { a, cc, dd };
          poly_cam(fb, V, t0, 3, fogged(TC.col[lv][(cj * N + ci) * 2], fogT), 0.5f, 1);
          poly_cam(fb, V, t1, 3, fogged(TC.col[lv][(cj * N + ci) * 2 + 1], fogT), 0.5f, 1);
        }
    }
}

/* ---- whitecaps ----------------------------------------------------------
 * Short foam streaks on a hashed grid round the camera, at three scales so
 * both a low pass and a high view show speed (the intro's drawWater). */
static bool water_at(const Map *m, float x, float z) {
  int i = (int)((x - m->origin_x) / m->spacing), j = (int)((z - m->origin_z) / m->spacing);
  if (i < 0 || j < 0 || i >= MAP_CELLS || j >= MAP_CELLS) return m->sea;
  int mi = m->mat[j * MAP_CELLS + i];
  return mi < m->npal && m->pal[mi].water && map_height(m, x, z) <= m->water_level + 0.5f;
}

void r3d_whitecaps(Framebuffer *fb, const View *V, float time) {
  if (!MAP || !MAP->sea || gpu_active()) return; /* the GPU's ocean has its own foam */
  const Camera *cam = &V->cam;
  float hgt = fmaxf(4, cam->pos.y - MAP->water_level);
  const float wx = sinf(250 * PI_F / 180), wz = cosf(250 * PI_F / 180);
  float scales[3] = { clampf(hgt * 0.07f, 1.5f, 30), clampf(hgt * 0.3f, 4, 80), clampf(hgt * 1.2f, 14, 300) };
  static const float AL[3][3] = { { 0.55f, 0.34f, 0.15f }, { 0.45f, 0.28f, 0.12f }, { 0.3f, 0.18f, 0.08f } };
  for (int si = 0; si < 3; si++) {
    float sc = scales[si];
    int R = si == 0 ? 30 : 34, ci = (int)floorf(cam->pos.x / sc), cj = (int)floorf(cam->pos.z / sc);
    for (int j = -R; j <= R; j++)
      for (int i = -R; i <= R; i++) {
        uint32_t n = ((uint32_t)(ci + i) * 73856093u) ^ ((uint32_t)(cj + j) * 19349663u) ^ ((uint32_t)(si + 7) * 83492791u);
        n = (n ^ (n >> 13)) * 1274126177u;
        n ^= n >> 16;
        if ((n & 255) > 52) continue;
        float x = (ci + i + ((n >> 8) & 255) / 255.0f) * sc, z = (cj + j + ((n >> 16) & 255) / 255.0f) * sc;
        float ph = ((n >> 24) & 255) / 255.0f, life = fmodf(time * 0.35f + ph, 1);
        if (life > 0.65f) continue;
        float Lh = sc * (0.25f + 0.35f * sinf(life / 0.65f * PI_F)) * 0.5f;
        vec3 a = to_cam(cam, v3(x - wx * Lh, MAP->water_level + 0.2f, z - wz * Lh));
        vec3 b = to_cam(cam, v3(x + wx * Lh, MAP->water_level + 0.2f, z + wz * Lh));
        if (a.z < near_z || b.z < near_z) continue;
        float dd = (a.z + b.z) / 2 / (sc * R);
        float x1 = sx_of(V, a), y1 = sy_of(V, a), x2 = sx_of(V, b), y2 = sy_of(V, b);
        if ((x1 < V->vx && x2 < V->vx) || (x1 > V->vx + V->vw && x2 > V->vx + V->vw) || (y1 < V->vy && y2 < V->vy) ||
            (y1 > V->vy + V->sh && y2 > V->vy + V->sh))
          continue;
        if (!water_at(MAP, x, z)) continue;
        fb_line_z(fb, x1, y1, x2, y2, rgb(236, 232, 240), 1, AL[si][dd < 0.3f ? 0 : dd < 0.6f ? 1 : 2], 2 / (a.z + b.z) * 1.001f);
      }
  }
}

/* ---- clouds ------------------------------------------------------------ */
static uint32_t puff_colour(const Cloud *C, const Puff *P) {
  float depth = fmaxf(1, C->top - C->base);
  vec3 n = v3_norm(v3(P->dx, P->dy - 0.35f * depth, P->dz));
  float h = clampf(P->dy / depth, 0, 1);
  float k = fmaxf(0, v3_dot(n, LIGHT));
  float o[3];
  for (int i = 0; i < 3; i++)
    o[i] = 255 * C->albedo * (L.amb[i] * (1 + 0.4f * h) + L.sun[i] * (0.45f + 0.6f * k) * (0.72f + 0.28f * h));
  return rgb(sat(o[0]), sat(o[1]), sat(o[2]));
}

/* Adds the puffs to the alpha list; returns how deep the camera is inside a
 * cloud (0 outside, 1 at a puff's centre) and that puff's colour. */
static const Cloud *extra_clouds;
static int nextra;
void r3d_extra_clouds(const Cloud *c, int n) {
  extra_clouds = c;
  nextra = n;
}
static const float (*clear_sky)[3];
static int nclear;
void r3d_clear_clouds(const float (*circles)[3], int n) {
  clear_sky = circles;
  nclear = n;
}
static bool cleared(const Cloud *C) {
  for (int i = 0; i < nclear; i++) {
    float dx = C->x - clear_sky[i][0], dz = C->z - clear_sky[i][1];
    if (dx * dx + dz * dz < clear_sky[i][2] * clear_sky[i][2]) return true;
  }
  return false;
}

int r3d_big_discs;

bool r3d_cloud_between(vec3 from, vec3 to) {
  if (!MAP) return false;
  const Weather *w = &MAP->weather[MAP_WX];
  vec3 d = v3_sub(to, from);
  float len = v3_len(d);
  if (len < 1) return false;
  d = v3_scale(d, 1 / len);
  for (int k = 0; k < w->nclouds + nextra; k++) {
    const Cloud *C = k < w->nclouds ? &w->clouds[k] : &extra_clouds[k - w->nclouds];
    if (k < w->nclouds && cleared(C)) continue;
    for (int q = 0; q < C->npuffs; q++) {
      const Puff *P = &C->puffs[q];
      if (P->tier > 1) continue;
      vec3 c = v3_sub(v3(C->x + P->dx, C->base + P->dy, C->z + P->dz), from);
      float t = v3_dot(c, d); /* the nearest point of the line of sight to the puff's middle */
      if (t < 0 || t > len) continue;
      float miss = v3_len(v3_sub(c, v3_scale(d, t)));
      if (miss < P->r * 0.85f) return true; /* well inside the disc drawn (the sprite's soft edge left out) */
    }
  }
  return false;
}

static float add_clouds(const View *V, uint32_t *inside_col) {
  const Weather *w = &MAP->weather[MAP_WX];
  float deep = 0;
  for (int k = 0; k < w->nclouds + nextra; k++) {
    const Cloud *C = k < w->nclouds ? &w->clouds[k] : &extra_clouds[k - w->nclouds];
    if (k < w->nclouds && cleared(C)) continue;
    vec3 cc = to_cam(&V->cam, v3(C->x, (C->base + C->top) / 2, C->z));
    float span = (C->top - C->base) + 2500;
    if (cc.z < -span || v3_len(cc) > FAR_Z + span) continue;
    for (int q = 0; q < C->npuffs; q++) {
      const Puff *P = &C->puffs[q];
      if (P->tier > 1) continue;
      vec3 wp = v3(C->x + P->dx, C->base + P->dy, C->z + P->dz);
      float dist = v3_dist(wp, V->cam.pos);
      if (dist < P->r) {
        float in = 1 - dist / P->r;
        if (in > deep) {
          deep = in;
          *inside_col = puff_colour(C, P);
        }
      }
      if (gpu_active()) {
        /* the film's clouds pass close: larger, thinner puffs overlap into one body, where whole ones show as discs */
        if (dist < FAR_Z) gpu_sprite(wp, P->r * (film_look || gpu_ultra() ? 1.5f : 1.15f), 0, puff_colour(C, P), film_look || gpu_ultra() ? 0.52f : 0.9f, SPR_PUFF);
        continue;
      }
      vec3 c = to_cam(&V->cam, wp);
      float rr = P->r * 1.0f;
      if (c.z < near_z + rr * 0.3f || c.z > FAR_Z) continue;
      float r = rr / c.z * V->F, x = sx_of(V, c), y = sy_of(V, c);
      if (x + r < V->vx || x - r > V->vx + V->vw || y + r < V->vy || y - r > V->vy + V->sh) continue;
      /* a puff close by fades out as the camera nears its edge, and the in-cloud haze takes over inside it:
       * drawn whole that close, it is a flat, hard-edged disc over half the screen */
      float a = 0.82f * clampf((dist - 1.2f * P->r) / (3.8f * P->r), 0, 1);
      if (a <= 0.01f) continue;
      if (r > V->sh * 0.5f && a > 0.4f) r3d_big_discs++;
      Item *it = new_item(c.z, IT_DISC);
      if (!it) continue; /* the list is full: no disc, but the rest of the puffs still count for the in-cloud haze */
      it->x = x;
      it->y = y;
      it->s = r;
      it->c = fogged(puff_colour(C, P), fog_at(c.z) * 0.85f);
      it->a = a;
    }
  }
  return deep;
}

static void add_lights(const View *V, float time) {
  /* the film's carrier sails on from where the map has her, so the map's deck and airfield lights would float on the
   * water behind her as dots; at its dawn the film shows none */
  if (film_look || L.lights <= 0.02f) return;
  for (int i = 0; i < MAP->nlights; i++) {
    const MapLight *l = &MAP->lights[i];
    if (l->blink > 0 && fmodf(time, l->blink) > l->blink * 0.5f) continue;
    vec3 c = to_cam(&V->cam, v3(l->x, l->y + 1, l->z));
    if (c.z < near_z || c.z > 14000) continue;
    if (gpu_active()) {
      gpu_sprite(v3(l->x, l->y + 1, l->z), l->size * 0.6f, 2.5f, l->color, L.lights, SPR_GLOW);
      continue;
    }
    Item *it = new_item(c.z - 2, IT_DOT);
    if (!it) return;
    it->x = sx_of(V, c);
    it->y = sy_of(V, c);
    it->s = fmaxf(1, fminf(4, l->size * 3 / c.z * V->F));
    it->c = hexc(l->color);
    it->a = L.lights;
  }
}

/* ---- cameras and views ------------------------------------------------ */
static Camera cam_for(const Game *g, const Plane *p, bool demo, float *zoom) {
  Camera c;
  *zoom = 1;
  if (!p->alive && p->hasDeath) {
    float t = g->time * 0.35f;
    c.pos = v3_add(p->deathPos, v3(cosf(t) * 140, 60, sinf(t) * 140));
    c.f = v3_norm(v3_sub(p->deathPos, c.pos));
    c.r = v3_norm(v3_cross(v3(0, 1, 0), c.f));
    c.u = v3_cross(c.f, c.r);
    c.ext = true;
    return c;
  }
  c.r = p->r;
  c.u = p->u;
  c.f = p->f;
  if (demo) {
    c.pos = v3_add(v3_add(v3_add(p->pos, v3_scale(p->f, -46)), v3_scale(p->u, 12)), v3_scale(p->r, 10));
    c.ext = true;
  } else if (p->chase) {
    c.pos = v3_add(v3_add(p->pos, v3_scale(p->f, -38)), v3_scale(p->u, 9));
    c.ext = true;
    if (!getenv("TL_CAM")) viewfx_camera(g, p, &c, zoom); /* the banking chase or the flyby (viewfx.c) */
    /* test pictures (TL_CAM=X,Y,Z): the chase camera at that point beside the jet (its right, up, forward,
     * metres), looking at it */
    const char *at = getenv("TL_CAM");
    float x, y, z;
    if (at && sscanf(at, "%f,%f,%f", &x, &y, &z) == 3) {
      c.pos = v3_add(p->pos, v3_add(v3_add(v3_scale(p->r, x), v3_scale(p->u, y)), v3_scale(p->f, z)));
      c.f = v3_norm(v3_sub(p->pos, c.pos));
      c.r = v3_norm(v3_cross(p->u, c.f));
      c.u = v3_cross(c.f, c.r);
    }
  } else {
    c.pos = v3_add(p->pos, v3_scale(p->u, 1.2f));
    c.ext = false;
    /* padlock: the pilot's eyes on the locked jet, or the nearest enemy, as far as a head turns in the seat
     * (165 degrees each way, from under the nose to over the head) */
    vec3 at;
    const Plane *e = p->lockTarget.kind == REF_PLANE ? &g->planes[p->lockTarget.idx] : NULL;
    if (!e || !e->alive || e->team == p->team) e = game_enemy_of((Game *)g, p->team, p->pos);
    if (p->ctl.look && e && e->alive) {
      at = v3_sub(e->pos, c.pos);
      float x = v3_dot(at, p->r), y = v3_dot(at, p->u), z = v3_dot(at, p->f);
      float yaw = clampf(atan2f(x, z), -2.88f, 2.88f), pit = clampf(atan2f(y, sqrtf(x * x + z * z)), -0.5f, 1.45f);
      c.f = v3_add(v3_add(v3_scale(p->f, cosf(pit) * cosf(yaw)), v3_scale(p->r, cosf(pit) * sinf(yaw))), v3_scale(p->u, sinf(pit)));
      c.r = v3_norm(v3_cross(p->u, c.f));
      c.u = v3_cross(c.f, c.r);
    }
  }
  /* keep a chase or death camera out of the ground */
  if (c.ext) {
    float h = terrain_h(c.pos.x, c.pos.z) + 3;
    if (c.pos.y < h) c.pos.y = h;
  }
  return c;
}

static const uint32_t PCOL[4][2][3] = {
  { { 255, 240, 140 }, { 200, 60, 20 } },
  { { 110, 110, 110 }, { 175, 175, 175 } },
  { { 40, 36, 30 }, { 40, 36, 30 } },
  { { 255, 255, 200 }, { 255, 160, 60 } },
};

static uint32_t flicker; /* cheap per-frame noise for flare colours */

static bool indoor;
void r3d_indoor(bool on) { indoor = on; }

void r3d_begin(Framebuffer *fb, const Camera *cam, int vx, int vy, int vw, int sh, float F, bool carriers, float time,
               View *out) {
  set_look();
  View V;
  V.cam = *cam;
  V.vx = (float)vx;
  V.vy = (float)vy;
  V.vw = (float)vw;
  V.sh = (float)sh;
  V.cx = vx + vw / 2.0f;
  V.cy = vy + sh / 2.0f;
  V.F = F > 0 ? F : fminf(vw * 0.75f, (float)sh);
  nitems = 0;

  fb_clip(fb, vx, vy, vw, sh);
  if (gpu_active()) {
    GpuEnv e = { 0 };
    e.sky_top = L.sky_top;
    e.sky_hor = L.sky_hor;
    e.fog = L.fog;
    e.fog_start = L.fog_start;
    e.fog_range = L.fog_range;
    e.fog_max = L.fog_max;
    e.far_ground = MAP ? (film_look || gpu_ultra() ? L.sky_hor : L.ground_far) : GROUND_FAR; /* in the film: no band of far land under the horizon */
    e.film_sea = film_look;
    e.time = time;
    e.map = MAP;
    e.classic = !MAP;
    if (MAP) {
      const TimeOfDay *t = &MAP->tod[MAP_TOD];
      const Weather *w = &MAP->weather[MAP_WX];
      e.sun_color = t->sun_color;
      e.sun_intensity = t->sun_intensity * w->sun_factor * 1.6f;
      e.ambient = t->ambient;
      e.ambient_intensity = t->ambient_intensity * w->ambient_factor;
      e.stars = t->stars;
      e.sun = t->sun;
    } else {
      e.sun_color = 0xfff4e0;
      e.sun_intensity = 1.5f;
      e.ambient = 0xb6cce6;
      e.ambient_intensity = 0.7f;
      e.sun = LIGHT;
    }
    if (indoor) { /* the hangar: a dark studio backdrop, a key light, no sun shadows, no haze */
      e.indoor = true;
      e.sky_top = 0x111619;
      e.sky_hor = 0x34414a;
      e.fog = 0x0c1012; /* below the eye line the backdrop darkens, not the haze */
      e.stars = 0;
      e.sun = LIGHT;
      e.sun_color = 0xfff0dc;
      e.sun_intensity = 1.3f;
      e.ambient = 0xd0d6e0;
      e.ambient_intensity = 1.05f;
      e.fog_start = 1e6f;
      e.fog_range = 1e7f;
    }
    gpu_view_begin(cam->pos, cam->r, cam->u, cam->f, (float)vx, (float)vy, (float)vw, (float)sh, V.F, near_z, &e);
    if (indoor) { /* the hangar shows the jet alone */
      *out = V;
      return;
    }
    draw_flats(fb, &V);
    const vec3 I = v3(1, 0, 0), U = v3(0, 1, 0), Fz = v3(0, 0, 1);
    if (!MAP)
      for (int i = 0; i < NUM_MOUNTAINS; i++)
        add_mesh(fb, &V, &MOUNTAINS[i].m, v3(MOUNTAINS[i].x, 0, MOUNTAINS[i].z), I, U, Fz, 1, 0, false);
    for (int i = 0; carriers && MAP && i < MAP->ncarriers; i++) {
      const Carrier *c = &MAP->carriers[i];
      float h = c->heading * PI_F / 180;
      add_mesh(fb, &V, &MESH_CARRIER, v3(c->x, MAP->water_level, c->z), v3(cosf(h), 0, -sinf(h)), U, v3(sinf(h), 0, cosf(h)), 1, 0, false);
    }
    *out = V;
    return;
  }
  if (indoor) { /* the hangar: a dark studio backdrop, lighter in the middle */
    for (int y = vy; y < vy + sh; y++) {
      float t = fabsf((y - vy) / (float)sh - 0.42f) * 2;
      fb_rect(fb, vx, y, vw, 1, mix(0x34414a, 0x111619, fminf(1, t)));
    }
    fb_clear_depth(fb, vx, vy, vw, sh);
    *out = V;
    return;
  }
  draw_sky_ground(fb, &V);
  fb_clear_depth(fb, vx, vy, vw, sh);
  if (L.map) {
    draw_sky_objects(fb, &V, time);
    draw_terrain(fb, &V);
  } else {
    draw_grid(fb, &V);
  }
  draw_flats(fb, &V);

  const vec3 I = v3(1, 0, 0), U = v3(0, 1, 0), Fz = v3(0, 0, 1);
  if (!MAP)
    for (int i = 0; i < NUM_MOUNTAINS; i++)
      add_mesh(fb, &V, &MOUNTAINS[i].m, v3(MOUNTAINS[i].x, 0, MOUNTAINS[i].z), I, U, Fz, 1, 0, false);
  for (int i = 0; carriers && MAP && i < MAP->ncarriers; i++) {
    const Carrier *c = &MAP->carriers[i];
    float h = c->heading * PI_F / 180;
    vec3 f = v3(sinf(h), 0, cosf(h)), r = v3(cosf(h), 0, -sinf(h));
    add_mesh(fb, &V, &MESH_CARRIER, v3(c->x, MAP->water_level, c->z), r, U, f, 1, 0, false);
  }
  *out = V;
}

void r3d_mesh(Framebuffer *fb, const View *V, const Mesh *M, vec3 pos, vec3 r, vec3 u, vec3 f, float s, float dim) {
  add_mesh_dim(fb, V, M, pos, r, u, f, s, 0x404050, true, dim);
}

void r3d_dot(const View *V, vec3 w, float sizeW, uint32_t col, float a, float minPx) { add_dot(V, w, sizeW, col, a, minPx); }

void r3d_ball(const View *V, vec3 w, float rad, uint32_t col, float a) {
  if (gpu_active()) {
    gpu_sprite(w, rad, 0.8f, col & 0xffffff, a, spr_kind);
    return;
  }
  vec3 c = to_cam(&V->cam, w);
  if (c.z < near_z) return;
  float r = rad / c.z * V->F;
  if (r < 0.4f) return;
  float x = sx_of(V, c), y = sy_of(V, c);
  if (x < V->vx - r || x > V->vx + V->vw + r || y < V->vy - r || y > V->vy + V->sh + r) return;
  Item *it = new_item(c.z, r < 0.8f ? IT_DOT : IT_DISC);
  if (!it) return;
  it->x = x;
  it->y = y;
  it->s = r < 0.8f ? 1 : fminf(r, V->sh * 3);
  it->c = L.map ? fogged(col, fog_at(c.z) * 0.7f) : col;
  it->a = a;
}

void r3d_seg(Framebuffer *fb, const View *V, vec3 a, vec3 b, uint32_t col, float wWorld, float alpha, float minW) {
  if (gpu_active()) {
    gpu_line(a, b, col & 0xffffff, alpha, line_glow);
    return;
  }
  vec3 ca = to_cam(&V->cam, a), cb = to_cam(&V->cam, b);
  if (ca.z < near_z && cb.z < near_z) return;
  if (ca.z < near_z) ca = cut_near(cb, ca);
  else if (cb.z < near_z) cb = cut_near(ca, cb);
  float z = (ca.z + cb.z) / 2, w = fminf(fmaxf(minW, wWorld / z * V->F), 12);
  fb_line_z(fb, sx_of(V, ca), sy_of(V, ca), sx_of(V, cb), sy_of(V, cb), col, w, alpha, 1 / z * 1.002f);
}

void r3d_end(Framebuffer *fb, const View *V, float time) {
  float inside = 0;
  uint32_t cloudCol = 0xffffff;
  if (MAP && !indoor) {
    add_lights(V, time);
    inside = add_clouds(V, &cloudCol);
  }
  if (gpu_active()) gpu_view_end();
  else draw_list(fb);
  if (inside > 0) fb_rect_a(fb, (int)V->vx, (int)V->vy, (int)V->vw, (int)V->sh, cloudCol, fminf(1, inside * 2.2f));
}

View r3d_last;
static float scene_F; /* the next scene's focal length in pixels; 0 for the default */

void render_scene(Framebuffer *fb, Game *g, const Camera *cam, int vx, int vy, int vw, int sh, View *out,
                  const Plane *hide) {
  View V;
  r3d_begin(fb, cam, vx, vy, vw, sh, scene_F, true, g->time, &V);
  scene_F = 0;
  r3d_whitecaps(fb, &V, g->time);
  const vec3 U = v3(0, 1, 0);
  if (g->def.on) {
    const Defense *d = &g->def;
    float h = d->heading * PI_F / 180;
    vec3 cf = v3(sinf(h), 0, cosf(h)), cr = v3(cosf(h), 0, -sinf(h));
    add_mesh(fb, &V, &MESH_BRIDGE, v3(d->pos.x, d->pos.y, d->pos.z), cr, U, cf, 1, 0, false);
    /* the captain's jet waits on catapult 2 until he takes it */
    if (d->commander && d->cmd_state != CMD_FLYING && d->cmd_plane >= 0) {
      const JetType *jt = &JETS[g->planes[d->cmd_plane].jet];
      add_mesh(fb, &V, &jt->mesh[0], defend_local(g, v3(8, d->deck + 1.9f, 66)), cr, U, cf, 1, TEAM[0].ui, true);
    }
    if (d->commander && d->cmd_state == CMD_WALK && !hide_captain) {
      float y = h + d->cap_yaw;
      add_mesh(fb, &V, &MESH_CREW, defend_local(g, d->cap), v3(cosf(y), 0, -sinf(y)), U, v3(sinf(y), 0, cosf(y)), 1, 0, false);
    }
  }
  for (int k = 0; g->def.on && k < g->def.nturrets; k++) {
    const Turret *t = &g->def.tur[k];
    vec3 at = defend_turret_pos(g, k);
    float ch_ = g->def.heading * PI_F / 180;
    add_mesh(fb, &V, t->kind == TUR_CIWS ? &MESH_CIWS_BASE : &MESH_SAM_BASE, at, v3(cosf(ch_), 0, -sinf(ch_)), U,
             v3(sinf(ch_), 0, cosf(ch_)), 1, 0, false);
    float y = t->yaw, pt = t->kind == TUR_SAM ? fmaxf(t->pitch, 0.5f) : t->pitch;
    vec3 f = v3(sinf(y) * cosf(pt), sinf(pt), cosf(y) * cosf(pt)), r = v3(cosf(y), 0, -sinf(y));
    add_mesh(fb, &V, t->kind == TUR_CIWS ? &MESH_CIWS_GUN : &MESH_SAM_BOX, v3_add(at, v3(0, 1.5f, 0)), r, v3_cross(f, r), f, 1, 0, false);
  }
  for (int i = 0; i < g->ntargets; i++) {
    const Target *t = &g->targets[i];
    const Mesh *M = &MESH_TARGET[t->team][t->alive ? t->type : TGT_RUBBLE];
    vec3 at = v3(t->pos.x, t->ground, t->pos.z);
    float h = t->heading * PI_F / 180;
    if (t->type == TGT_RADAR && t->alive) h = t->spin;
    add_mesh(fb, &V, M, at, v3(cosf(h), 0, -sinf(h)), U, v3(sinf(h), 0, cosf(h)), 1, 0, false);
  }
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (!q->alive || q == hide) continue;
    gloss_override = 1;
    add_mesh(fb, &V, stores_bare(q->jet, q->team), q->pos, q->r, q->u, q->f, 1, TEAM[q->team].ui, true);
    surfaces_draw(fb, &V, g, q);
    gloss_override = -1;
    /* far off, a jet is a speck of a pixel or two that a cloud swallows; the eye still finds the dark shape, so it
     * keeps at least a few pixels of dark silhouette (near, the mesh is the jet) */
    if (to_cam(&V.cam, q->pos).z > 800) {
      spr_kind = SPR_SOFT;
      add_dot(&V, q->pos, 3, hexc(0x2a2e34), 0.9f, gpu_active() ? 6 : 2); /* a soft sprite's width / a disc's radius */
    }
    stores_jet(fb, &V, g, q);
    stores_gear(fb, &V, q);
    stores_exhaust(&V, g, q);
  }
  for (int t = 0; t < 2; t++) { /* each side's tanker, while it is up */
    const Tanker *T = &g->tankers[t];
    if (T->gone) continue;
    add_mesh(fb, &V, &MESH_TANKER, T->pos, T->r, T->u, T->f, 3.5f, 0xd8d8d8, true);
    draw_line(fb, &V, v3_add(T->pos, v3_scale(T->f, -18)), T->drogue, hexc(0x2a2a2a), 1);
    add_dot(&V, T->drogue, 2.5f, hexc(0xf2f2f2), 1, 2);
  }
  stores_missiles(fb, &V, g); /* the missiles, their flames and trails (stores.c) */
  line_glow = 6;
  for (int i = 0; i < g->nbullets; i++) {
    const Bullet *b = &g->bullets[i];
    draw_line(fb, &V, b->pos, v3_sub(b->pos, v3_scale(b->vel, 0.018f)), hexc(0xffe680), 1);
  }
  line_glow = 1;
  spr_kind = SPR_GLOW;
  for (int i = 0; i < MAX_FLARES; i++) {
    const Flare *f = &g->flares[i];
    if (!f->live) continue;
    flicker = flicker * 1664525u + 1013904223u;
    if (f->chaff) { /* a glinting cloud of foil strips */
      spr_kind = SPR_SOFT;
      add_dot(&V, f->pos, 9, hexc(0xb8c0cc), 0.35f, 2);
      add_dot(&V, v3_add(f->pos, v3((float)(flicker & 7) - 3.5f, 0, (float)((flicker >> 3) & 7) - 3.5f)), 1.5f, hexc(0xffffff), 1, 1);
      spr_kind = SPR_GLOW;
      continue;
    }
    add_dot(&V, f->pos, 6, hexc((flicker >> 31) ? 0xffffff : 0xffe9a0), 1, 2);
  }
  for (int i = 0; i < g->nparts; i++) {
    const Particle *q = &g->parts[i];
    if (q->type == 1 && q->max == 1.6f && q->grow == 9) continue; /* a missile's puff: stores.c draws its trail */
    spr_kind = q->type == 0 || q->type == 3 ? SPR_GLOW : SPR_SOFT;
    float t = 1 - q->life / q->max;
    const uint32_t(*c)[3] = PCOL[q->type];
    float a = q->type == 1 ? 0.55f * (1 - t) : 1 - t * 0.6f;
    add_dot(&V, q->pos, q->size,
            rgb((uint8_t)lerpf((float)c[0][0], (float)c[1][0], t), (uint8_t)lerpf((float)c[0][1], (float)c[1][1], t),
                (uint8_t)lerpf((float)c[0][2], (float)c[1][2], t)),
            a, 1);
  }
  viewfx_update(g); /* contrails, vapour, spray, steam: for the eye only (viewfx.c) */
  viewfx_draw(fb, &V, g);
  spr_kind = SPR_SOFT;
  r3d_end(fb, &V, g->time);
  r3d_last = V;
  if (out) *out = V;
}

void render_view(Framebuffer *fb, Game *g, Plane *p, int vx, int vy, int vw, int vh, ViewOpts opts) {
  if (p->hidx == 1 && command_active(g)) {
    command_render(fb, g, vx, vy, vw, vh);
    return;
  }
  /* the outside views (chase, flyby) show the picture whole: the HUD, no instrument panel */
  bool outside = p->chase && p->alive && !opts.demo && !(g->classic && p->mapview);
  int ph = opts.panel && !outside ? (int)roundf(fmaxf(52, fminf(vh * 0.24f, vw * 0.21f))) : 0;
  int sh = vh - ph;
  if (g->classic && p->mapview && p->alive && !opts.demo) {
    fb_clip(fb, vx, vy, vw, sh);
    hud_map(fb, g, p, (float)vx, (float)vy, (float)vw, (float)sh);
    fb_clip_reset(fb);
    if (opts.panel) {
      fb_clip(fb, vx, vy + sh, vw, ph);
      hud_panel(fb, g, p, (float)vx, (float)(vy + sh), (float)vw, (float)ph);
      fb_clip_reset(fb);
    }
    return;
  }
  float zoom = 1;
  Camera cam = cam_for(g, p, opts.demo, &zoom);
  View V;
  /* from the cockpit, a pilot's field of view: about 88 degrees across a
   * 16:9 view and 45 up and down (the old view was 99 by 53, a fisheye) */
  if (!cam.ext) scene_F = fminf(vw * 0.5f / tanf(44 * PI_F / 180), sh * 0.5f / tanf(22.5f * PI_F / 180));
  else if (zoom > 1) scene_F = fminf(vw * 0.75f, (float)sh) * zoom; /* the flyby's long lens */
  if (!opts.demo) {
    float wide = 1;
    viewfx_speed(g, p, &cam, &wide);
    scene_F = (scene_F > 0 ? scene_F : fminf(vw * 0.75f, (float)sh)) * wide;
  }
  render_scene(fb, g, &cam, vx, vy, vw, sh, &V, cam.ext ? NULL : p);

  HudView hv = { V.cam.pos, V.cam.r, V.cam.u, V.cam.f, V.vx, V.vy, V.vw, V.sh, V.cx, V.cy, V.F };
  if (!opts.demo) hud_draw(fb, g, p, &hv, opts.label);
  fb_clip_reset(fb);
  if (ph > 0) {
    fb_clip(fb, vx, vy + sh, vw, ph);
    hud_panel(fb, g, p, (float)vx, (float)(vy + sh), (float)vw, (float)ph);
    fb_clip_reset(fb);
  }
}

void render_game(Framebuffer *fb, Game *g) {
  int W = fb->w, H = fb->h;
  fb_clear(fb, gpu_active() ? 0 : 0xFF000000u); /* on the GPU the framebuffer is a see-through HUD layer */
  ViewOpts one = { true, false, false }, two = { true, true, false }, demo = { false, false, true };
  if (g->nlocals == 1) {
    render_view(fb, g, &g->planes[g->locals[0]], 0, 0, W, H, one);
  } else if (g->nlocals == 2) {
    Plane *L0 = &g->planes[g->locals[0]], *L1 = &g->planes[g->locals[1]];
    if (W >= H * 1.15f) {
      int hw = W / 2;
      render_view(fb, g, L0, 0, 0, hw - 1, H, two);
      render_view(fb, g, L1, hw + 1, 0, W - hw - 1, H, two);
      fb_rect(fb, hw - 1, 0, 2, H, hexc(0x6C5EB5));
    } else {
      int hh = H / 2;
      render_view(fb, g, L0, 0, 0, W, hh - 1, two);
      render_view(fb, g, L1, 0, hh + 1, W, H - hh - 1, two);
      fb_rect(fb, 0, hh - 1, W, 2, hexc(0x6C5EB5));
    }
  } else {
    render_view(fb, g, &g->planes[g->focus], 0, 0, W, H, demo);
  }
}
