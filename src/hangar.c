/* hangar.c - the jet select and loadout screen's picture: the chosen jet
 * turning on a dark studio backdrop, as on the hangar model preview. It is
 * drawn high above the world so nothing outside reaches in. */
#include <math.h>

#include "core/jets.h"
#include "core/world.h"
#include "gpu.h"
#include "render3d.h"

#define BASE_Y 20000.0f

void render_hangar(Framebuffer *fb, int jet, const int *load, float time) {
  int W = fb->w, H = fb->h;
  (void)load;
  fb_clear(fb, gpu_active() ? 0 : 0xFF000000u);
  if (jet < 0 || jet >= NJETS) return;
  const JetType *j = &JETS[jet];
  const Mesh *M = &j->mesh[0];
  float minx = 0, maxx = 0, miny = 0, maxy = 0, minz = 0, maxz = 0;
  for (int i = 0; i < M->nv; i++) {
    minx = fminf(minx, M->v[i].x);
    maxx = fmaxf(maxx, M->v[i].x);
    miny = fminf(miny, M->v[i].y);
    maxy = fmaxf(maxy, M->v[i].y);
    minz = fminf(minz, M->v[i].z);
    maxz = fmaxf(maxz, M->v[i].z);
  }
  /* as on the hangar model preview: the jet alone on a dark studio
   * backdrop, turning slowly, seen from a three-quarter view above */
  vec3 c = v3(-(minx + maxx) / 2, BASE_Y - (miny + maxy) / 2, -(minz + maxz) / 2);
  float size = fmaxf(maxz - minz, maxx - minx);
  float a = 2.4f + time * 0.25f, R = size * 1.45f + 4;
  vec3 eye = v3(sinf(a) * R, BASE_Y + R * 0.36f, cosf(a) * R);
  vec3 look = v3(0, BASE_Y, 0);
  vec3 f = v3_norm(v3_sub(look, eye));
  vec3 r = v3_norm(v3_cross(v3(0, 1, 0), f));
  /* the jet sits in the left part of the screen, the sheet on the right */
  f = v3_norm(v3_add(f, v3_scale(r, 0.36f)));
  f = v3_norm(v3_add(f, v3(0, -0.13f, 0))); /* and above the tables under it */
  r = v3_norm(v3_cross(v3(0, 1, 0), f));
  Camera cam = { eye, r, v3_cross(f, r), f, true };
  vec3 light = LIGHT;
  LIGHT = v3_norm(v3(-0.35f, 0.85f, 0.4f)); /* a studio key light: front, left and high */
  r3d_indoor(true);
  View V;
  r3d_begin(fb, &cam, 0, 0, W, H, H * 0.95f, false, time, &V);
  r3d_gloss(1);
  r3d_mesh(fb, &V, M, c, v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1), 1, 1);
  r3d_gloss(-1);
  r3d_end(fb, &V, time);
  r3d_indoor(false);
  LIGHT = light;
}
