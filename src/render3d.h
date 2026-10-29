/* render3d.h - camera, near clip, sky and ground, grid, per-view draw (JS lines 3282-3445, 3662-3680). */
#ifndef A4C_RENDER3D_H
#define A4C_RENDER3D_H

#include <stdbool.h>

#include "core/game.h"
#include "raster.h"

typedef struct { vec3 pos, r, u, f; bool ext; } Camera;

/* One viewport: camera plus where it lands on screen. sh is the height of
 * the 3D part (the cockpit panel sits below it). */
typedef struct {
  Camera cam;
  float  vx, vy, vw, sh, cx, cy, F;
} View;

typedef struct { bool panel, label, demo; } ViewOpts;

static inline vec3 to_cam(const Camera *c, vec3 w) {
  vec3 d = v3_sub(w, c->pos);
  return v3(v3_dot(d, c->r), v3_dot(d, c->u), v3_dot(d, c->f));
}

/* Building blocks for scenes that are not a match (the intro): begin draws
 * sky, terrain, runways (and the map's carriers); then meshes, balls, dots
 * and segments; end adds clouds and night lights and draws everything with
 * alpha. F is the focal length in pixels (0: the game's). */
void r3d_begin(Framebuffer *fb, const Camera *cam, int vx, int vy, int vw, int sh, float F, bool carriers, float time,
               View *out);
void r3d_mesh(Framebuffer *fb, const View *V, const Mesh *M, vec3 pos, vec3 r, vec3 u, vec3 f, float s, float dim);
void r3d_dot(const View *V, vec3 w, float sizeW, uint32_t col, float a, float minPx);
void r3d_ball(const View *V, vec3 w, float rad, uint32_t col, float a);
void r3d_seg(Framebuffer *fb, const View *V, vec3 a, vec3 b, uint32_t col, float wWorld, float alpha, float minW);
void r3d_end(Framebuffer *fb, const View *V, float time);
/* Foam streaks on open sea round the camera. */
void r3d_whitecaps(Framebuffer *fb, const View *V, float time);
/* From inside the bridge the island's own walls are left out (the bridge
 * mesh stands in for them); the captain's own body is left out of his view. */
void r3d_inside_bridge(bool on);
void r3d_near(float z); /* near plane distance (default NEAR_Z) */
/* For the GPU renderer: how the next sprites look (SPR_* in gpu.h), and a
 * gloss to force on the next meshes (-1: decide by mesh). */
void r3d_sprite_kind(int kind);
void r3d_gloss(float g);
void r3d_hide_captain(bool on);
/* Puffs drawn on top of the map's weather (the intro's climb cloud). */
void r3d_extra_clouds(const Cloud *c, int n);
/* Map clouds whose centre lies in one of these [x, z, r] circles are left out. */
void r3d_clear_clouds(const float (*circles)[3], int n);

/* Draws the world from cam into the viewport (no HUD); hide skips one jet
 * (the cockpit view's own). The intro uses this too. */
/* The loadout screen's backdrop (hangar.c): the jet (index into JETS) on
 * its gear in a hardened shelter, with the arming (heat, radar,
 * air-to-ground counts) on trolleys. */
void render_hangar(Framebuffer *fb, int jet, const int *load, float time);
/* Indoor lighting for the next view: floodlights, no sun shadows or haze. */
void r3d_indoor(bool on);
void render_scene(Framebuffer *fb, Game *g, const Camera *cam, int vx, int vy, int vw, int sh, View *out,
                  const Plane *hide);
void render_view(Framebuffer *fb, Game *g, Plane *p, int vx, int vy, int vw, int vh, ViewOpts opts);
/* Every local seat's view, split screen for two, or the demo fly-by. */
void render_game(Framebuffer *fb, Game *g);

#endif
