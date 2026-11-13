/* hud.c - gunsight, heading tape, jet markers, lock diamond, warnings,
 * cockpit panel, radar scope and attitude ball (JS drawHUD, drawPanel, bar,
 * lamp, radarScope, attitude; lines 3487-3660). */
#include "hud.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "core/sim.h"
#include "core/world.h"

static inline uint32_t C(uint32_t c) { return hexc(c); }

HudMarks hud_marks;
HudView      hud_view;
const Plane *hud_view_p;

static vec3 hud_cam(const HudView *V, vec3 w) {
  vec3 d = v3_sub(w, V->pos);
  return v3(v3_dot(d, V->r), v3_dot(d, V->u), v3_dot(d, V->f));
}

/* a label centred over a mark, kept whole inside the view: a tanker near the windscreen's edge had its
 * "TANKER 9.8KM" cut to "KER 9.8KM" by the view's clip */
static float label_x(const char *s, float sx, float vx, float vw, int scale) {
  float half = text_width(s, scale) / 2.0f + scale;
  return half * 2 >= vw ? vx + vw / 2 : clampf(sx, vx + half, vx + vw - half);
}

static bool film_labels;
static int sight_styles[2] = { SIGHT_CROSS, SIGHT_CROSS }, sight_style = SIGHT_CROSS;
void hud_show_sight(int seat, int style) {
  if (seat >= 0 && seat < 2) sight_styles[seat] = style;
}
void hud_film_labels(bool on) { film_labels = on; }

/* a label's box, for the messages under the sight to keep clear of (the tanker's "TANKER 9.8KM" had "BINGO - BASE
 * 2 KM" written over it) */
static void keep_clear(float (*lab)[4], int *nlab, const char *s, float x, float y, int ks) {
  if (*nlab >= 12) return;
  float hw = text_width(s, ks) / 2.0f + 2.0f * ks, hh = 6.0f * ks;
  lab[*nlab][0] = x - hw, lab[*nlab][1] = y - hh, lab[*nlab][2] = x + hw, lab[*nlab][3] = y + hh;
  (*nlab)++;
}

/* and its line 8 HUD pixels over the mark, or under it near the view's top (a tanker near the top had its label
 * cut off by the view's clip, or wholly outside it) */
static float label_y(float sy, float vy, float sh, float k) {
  float y = sy - 8 * k;
  if (y - 4 * k < vy) y = sy + 8 * k;
  return fminf(y, vy + sh - 4 * k);
}

/* and up a line at a time (down near the top) past the labels already written: an enemy's range label under its
 * box ran into the tanker's ("3.TANKER 19.7KM") */
static float label_clear(const char *s, float x, float y, float vy, float sh, int ks, float (*lab)[4], int nlab) {
  float hw = text_width(s, ks) / 2.0f, hh = 3.5f * ks, step = y - 8.0f * ks < vy ? 10.0f * ks : -10.0f * ks;
  for (int tries = 0; tries < 4; tries++) {
    bool hit = false;
    for (int j = 0; j < nlab && !hit; j++) hit = x + hw > lab[j][0] && x - hw < lab[j][2] && y + hh > lab[j][1] && y - hh < lab[j][3];
    if (!hit || y + step - hh < vy || y + step + hh > vy + sh) break;
    y += step;
  }
  return y;
}

/* the hints at the start and the corner speed's, over the haze: DIM (0x8a83c9) was within 53 of a hazy sky's
 * 0x7d98b6, set apart by its outline alone */
#define HINT INK

static void tri(Framebuffer *fb, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
  float xy[6] = { x0, y0, x1, y1, x2, y2 };
  fb_poly(fb, xy, 3, c, 1, 0);
}

/* What G does to the pilot's eyes: grey-out narrows into a black tunnel,
 * negative G washes red; both go all the way during a G-LOC. */
static void g_vision(Framebuffer *fb, const Plane *p, const HudView *V) {
  float black = clampf((p->gstress - 1) / 4, 0, 1), red = clampf(p->gneg / 3.5f, 0, 1);
  if (p->gloc > 0) {
    if (p->gneg > p->gstress) red = 1;
    else black = 1;
  }
  if (black <= 0.01f && red <= 0.01f) return;
  int x0 = (int)V->vx, y0 = (int)V->vy, x1 = (int)(V->vx + V->vw), y1 = (int)(V->vy + V->sh);
  /* inside the clip as well: the rows are written straight (row[x]), not through the clipped calls */
  if (x0 < fb->cx0) x0 = fb->cx0;
  if (y0 < fb->cy0) y0 = fb->cy0;
  if (x1 > fb->cx1) x1 = fb->cx1;
  if (y1 > fb->cy1) y1 = fb->cy1;
  float diag = sqrtf(V->vw * V->vw + V->sh * V->sh) * 0.5f;
  float r_in = diag * (1 - black) * 0.7f, r_out = r_in + diag * 0.35f;
  for (int y = y0; y < y1; y++) {
    uint32_t *row = fb->px + (size_t)y * fb->w;
    float dy = y + 0.5f - V->cy;
    for (int x = x0; x < x1; x++) {
      float dx = x + 0.5f - V->cx, d = sqrtf(dx * dx + dy * dy);
      if (black > 0 && (row[x] >> 24) == 255) {
        /* an opaque (software) picture greys out first */
        uint32_t c = row[x];
        float r = (float)((c >> 16) & 255), gg = (float)((c >> 8) & 255), b = (float)(c & 255);
        float grey = (r + gg + b) / 3, k = black * 0.8f;
        row[x] = rgb((uint8_t)(r + (grey - r) * k), (uint8_t)(gg + (grey - gg) * k), (uint8_t)(b + (grey - b) * k));
      }
      if (black > 0) {
        float t = black >= 1 ? 1 : clampf((d - r_in) / (r_out - r_in), 0, 1);
        if (t > 0) fb_blend_px(fb, x, y, 0xFF000000u, t);
      }
      if (red > 0) fb_blend_px(fb, x, y, rgb(150, 0, 0), red * 0.7f);
    }
  }
}

/* The gunsight on the nose and the heading tape over it. */
static void sight_and_tape(Framebuffer *fb, const Plane *p, float cx, float cy, float vy, float vw, int ks) {
  const float k = (float)ks;
  char buf[16];
  /* gunsight */
  float gp = 4 * k, l = 9 * k;
  switch (sight_style) {
  case SIGHT_CROSS:
    fb_line(fb, cx - l, cy, cx - gp, cy, C(HUDC), k, 1);
    fb_line(fb, cx + gp, cy, cx + l, cy, C(HUDC), k, 1);
    fb_line(fb, cx, cy - l, cx, cy - gp, C(HUDC), k, 1);
    fb_line(fb, cx, cy + gp, cx, cy + l, C(HUDC), k, 1);
    fb_rect(fb, (int)(cx - k / 2), (int)(cy - k / 2), ks, ks, C(HUDC));
    break;
  case SIGHT_CIRCLE:
  case SIGHT_RING_DOT:
    /* a ring as wide as the cross, in 24 straight pieces */
    for (int s = 0; s < 24; s++) {
      float a0 = s * (2 * PI_F / 24), a1 = (s + 1) * (2 * PI_F / 24);
      fb_line(fb, cx + cosf(a0) * l * 0.8f, cy + sinf(a0) * l * 0.8f, cx + cosf(a1) * l * 0.8f, cy + sinf(a1) * l * 0.8f,
              C(HUDC), k, 1);
    }
    if (sight_style == SIGHT_RING_DOT) fb_rect(fb, (int)(cx - k), (int)(cy - k), 2 * ks, 2 * ks, C(HUDC));
    break;
  case SIGHT_TRIANGLE: {
    /* a chevron's point on the nose: a triangle pointing up, its tip on the aim point */
    float h = l * 0.9f;
    fb_line(fb, cx, cy, cx - h, cy + h, C(HUDC), k, 1);
    fb_line(fb, cx, cy, cx + h, cy + h, C(HUDC), k, 1);
    fb_line(fb, cx - h, cy + h, cx + h, cy + h, C(HUDC), k, 1);
    break;
  }
  case SIGHT_DOT:
    fb_rect(fb, (int)(cx - k), (int)(cy - k), 2 * ks, 2 * ks, C(HUDC));
    break;
  default: break; /* off */
  }

  /* heading tape */
  float hdg = fmodf(atan2f(p->f.x, p->f.z) * 180 / PI_F + 360, 360);
  float tw = fminf(vw * 0.5f, 160 * k), ty = vy + 8 * k;
  for (int d = (int)ceilf((hdg - 30) / 5) * 5; d <= hdg + 30; d += 5) {
    float x = cx + (d - hdg) / 30 * tw / 2;
    fb_line(fb, x, ty + 6 * k, x, ty + (d % 10 == 0 ? 2 : 4) * k, C(HUDC), k, 1);
    int dd = ((d % 360) + 360) % 360;
    if (dd % 90 == 0) {
      char s[2] = { "NESW"[dd / 90], 0 };
      fb_text(fb, s, x, ty - 2 * k, ks, C(HUDC), ALIGN_CENTER, 2);
    }
  }
  snprintf(buf, sizeof buf, "%03d", ((int)roundf(hdg)) % 360);
  fb_text(fb, buf, cx, ty + 13 * k, ks, C(HUDC), ALIGN_CENTER, 2);
}

void hud_draw(Framebuffer *fb, Game *g, Plane *p, const HudView *V, bool label) {
  const float vx = V->vx, vy = V->vy, vw = V->vw, sh = V->sh, cx = V->cx, cy = V->cy;
  int ks = (int)fmaxf(1, floorf(fminf(vw / 300, sh / 150)));
  const float k = (float)ks;
  bool blink = ((int)(g->time * 4)) % 2 == 0;
  char buf[64];
  float lab[12][4]; /* the range labels under the enemy boxes, the tankers' labels and the lock with its LOCK */
  int nlab = 0;

  if (!hud_view_p) { /* the first view drawn: player 1's, when the screen is split */
    hud_view = *V;
    hud_view_p = p;
  }
  if (p->alive && g->g_effects) g_vision(fb, p, V);
  if (label) fb_text(fb, p->name, vx + 6 * k, vy + 8 * k, ks, C(TEAM[p->team].ui), ALIGN_LEFT, 2);
  if (p->alive && p->gloc > 0) {
    if (blink) fb_text(fb, p->gneg > p->gstress ? "REDOUT" : "G-LOC", cx, cy, 2 * ks, C(WARN), ALIGN_CENTER, 2);
  } else if (p->alive) {
    HudMarks *M = &hud_marks;
    memset(M, 0, sizeof *M);
    M->p = p;
    M->sx = cx;
    M->sy = cy;
    /* G meter */
    snprintf(buf, sizeof buf, "G %+.1f", p->gmeter);
    uint32_t gc = p->gmeter > 8.5f || p->gmeter < -2 ? WARN : p->gmeter > 7 ? AMBER : HUDC;
    fb_text(fb, buf, vx + 8 * k, cy, ks, C(gc), ALIGN_LEFT, 2);
    /* the armed weapon under it: what the missile key fires next (modern), or the selected weapon (classic) */
    if (g->classic && !p->is_ai) {
      static const char *const WN[W_COUNT] = { "GUN", "HEAT", "RADAR", "A-G" };
      int n = p->wsel == W_CANNON ? (int)p->ammo : p->wsel == W_HEAT ? p->missiles : p->wsel == W_RADAR ? p->radar : p->ground;
      snprintf(buf, sizeof buf, "%s %d", WN[p->wsel], n);
    } else if (p->missiles > 0) snprintf(buf, sizeof buf, "HEAT %d", p->missiles);
    else if (p->radar > 0) snprintf(buf, sizeof buf, "RADAR %d", p->radar);
    else snprintf(buf, sizeof buf, "NO MSL");
    fb_text(fb, buf, vx + 8 * k, cy + 10 * k, ks, C(HUDC), ALIGN_LEFT, 2);
    /* realistic: the gear (locked down, up, or on its way) and the flaps */
    if (g->realistic && !p->drone) {
      bool moving = p->gearT > 0 && p->gearT < 1;
      fb_text(fb, moving ? "GEAR TRANSIT" : p->gearT >= 1 ? "GEAR DOWN" : "GEAR UP", vx + 8 * k, cy + 20 * k, ks,
              C(moving ? AMBER : p->gearT >= 1 ? HUDC : DIM), ALIGN_LEFT, 2);
      static const char *const FL[3] = { "FLAPS UP", "FLAPS T/O", "FLAPS LDG" };
      int fn = (int)roundf(p->flapT);
      bool fmove = fabsf(p->flapT - (float)fn) > 0.01f || fn != p->flaps;
      fb_text(fb, FL[fn < 0 ? 0 : fn > 2 ? 2 : fn], vx + 8 * k, cy + 30 * k, ks, C(fmove ? AMBER : p->flapT > 0.5f ? HUDC : DIM),
              ALIGN_LEFT, 2);
    }
    /* the tanker call (key 4): READY, INBOUND, ON STATION, USED or LOST, under the weapon and the gear */
    if (!p->drone) {
      const char *ts = sim_tanker_status(g, p);
      uint32_t tc = !strcmp(ts, "TANKER READY") ? HUDC : strstr(ts, "LOST") ? WARN : strstr(ts, "USED") || strstr(ts, "NO ") ? DIM : AMBER;
      fb_text(fb, ts, vx + 8 * k, cy + (g->realistic ? 40 : 20) * k, ks, C(tc), ALIGN_LEFT, 2);
    }
    /* the readouts' block at the left (G, the weapon, gear and flaps, the tanker call), for the range labels to keep
     * clear of: kept inside the view, an enemy's range label at the left edge ran over "G -1.2" */
    float rb[4] = { vx + 8 * k - 2 * k, cy - 6 * k, vx + 8 * k + text_width("TANKER ON STATION", ks) + 2 * k, cy + (g->realistic ? 46 : 26) * k };
    /* padlocked: the eyes are off the nose, so no gunsight or heading tape; a line back to the nose */
    bool padlock = p->ctl.look && !p->chase && v3_dot(V->f, p->f) < 0.999f;
    if (padlock) {
      fb_text(fb, "PADLOCK", cx, vy + 8 * k, ks, C(AMBER), ALIGN_CENTER, 2);
      vec3 nc = hud_cam(V, v3_add(V->pos, v3_scale(p->f, 1000)));
      float ang = atan2f(-nc.y, nc.x);
      if (nc.z > NEAR_Z && fabsf(nc.x / nc.z * V->F) < vw / 2 && fabsf(nc.y / nc.z * V->F) < sh / 2)
        fb_ring(fb, cx + nc.x / nc.z * V->F, cy - nc.y / nc.z * V->F, 5 * k, C(HUDC), k);
      else
        fb_text(fb, "NOSE", cx + cosf(ang) * (vw / 2 - 30 * k), cy + sinf(ang) * (sh / 2 - 20 * k), ks, C(HUDC), ALIGN_CENTER, 2);
    }
    /* the second local seat's own gunsight; on a network every computer's own player is its first seat */
    sight_style = sight_styles[g->nlocals > 1 && p == &g->planes[g->locals[1]] ? 1 : 0];
    if (!padlock) sight_and_tape(fb, p, cx, cy, vy, vw, ks);

    /* jet markers: brackets for enemies, a tag for wingmen, an edge arrow for the nearest enemy */
    Plane *nearest = game_enemy_of(g, p->team, p->pos);
    for (int i = 0; i < g->nplanes; i++) {
      Plane *e = &g->planes[i];
      if (e == p || !e->alive) continue;
      bool foe = e->team != p->team;
      vec3 c = hud_cam(V, e->pos);
      float d = v3_dist(e->pos, p->pos);
      uint32_t col = C(TEAM[e->team].ui);
      if (foe && film_labels && e != nearest) buf[0] = 0; /* in the film two contacts close together wrote their ranges over each other */
      else if (foe) snprintf(buf, sizeof buf, "%.1fKM", d / 1000);
      else snprintf(buf, sizeof buf, "%s", e->name);
      float sx = c.z > NEAR_Z ? cx + c.x / c.z * V->F : 0, sy = c.z > NEAR_Z ? cy - c.y / c.z * V->F : 0;
      float m = 14 * k;
      if (!foe) {
        if (c.z > NEAR_Z && sx > vx && sx < vx + vw && sy > vy && sy < vy + sh) {
          fb_rect_stroke(fb, sx - 3 * k, sy - 3 * k, 6 * k, 6 * k, C(HUDC), k);
          fb_text(fb, buf, sx, sy - 8 * k, ks, C(HUDC), ALIGN_CENTER, 2);
        }
        continue;
      }
      bool onScreen = c.z > NEAR_Z && sx > vx + m && sx < vx + vw - m && sy > vy + m && sy < vy + sh - m;
      if (e != nearest && !onScreen) continue;
      if (onScreen) {
        if (M->nfoe < MAX_PLANES) M->foe[M->nfoe++] = (HudFoe){ i, sx, sy, d / 1000 };
        float s = fmaxf(6 * k, 9 / c.z * V->F);
        static const int A[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int q = 0; q < 4; q++) {
          float ax = (float)A[q][0], ay = (float)A[q][1];
          float px = sx + ax * s, py = sy + ay * s;
          fb_line(fb, px, py - ay * s * 0.45f, px, py, col, k, 1);
          fb_line(fb, px, py, px - ax * s * 0.45f, py, col, k, 1);
        }
        /* whole inside the view, as the tankers' (near the edge "12.3KM" ran off the picture) */
        float rx = label_x(buf, sx, vx, vw, ks), ry = sy + s + 6 * k, rhw = text_width(buf, ks) / 2.0f;
        if (rx - rhw < rb[2] && rx + rhw > rb[0] && ry - 4 * k < rb[3] && ry + 4 * k > rb[1]) rx = rb[2] + rhw + k;
        fb_text(fb, buf, rx, sy + s + 6 * k, ks, col, ALIGN_CENTER, 2);
        if (nlab < 12) { /* the range label's place, for the messages to keep clear of */
          float hw = text_width(buf, ks) / 2.0f + 2 * k;
          lab[nlab][0] = rx - hw, lab[nlab][1] = sy + s + 6 * k - 6 * k, lab[nlab][2] = rx + hw, lab[nlab][3] = sy + s + 6 * k + 6 * k;
          nlab++;
        }
      } else {
        float ang = atan2f(-c.y, c.x);
        float ex = cx + cosf(ang) * (vw / 2 - m), ey = cy + sinf(ang) * (sh / 2 - m);
        tri(fb, ex + cosf(ang) * 7 * k, ey + sinf(ang) * 7 * k, ex + cosf(ang + 2.5f) * 6 * k,
            ey + sinf(ang + 2.5f) * 6 * k, ex + cosf(ang - 2.5f) * 6 * k, ey + sinf(ang - 2.5f) * 6 * k, col);
        /* its range off the readouts at the left (at the left edge it was written over "G -1.2"): over or under them */
        float ax = ex - cosf(ang) * 14 * k, ay = ey - sinf(ang) * 10 * k, ahw = text_width(buf, ks) / 2.0f;
        if (ax - ahw < rb[2] && ax + ahw > rb[0] && ay - 4 * k < rb[3] && ay + 4 * k > rb[1])
          ay = ay < (rb[1] + rb[3]) / 2 ? rb[1] - 5 * k : rb[3] + 5 * k;
        fb_text(fb, buf, ax, ay, ks, col, ALIGN_CENTER, 2);
      }
    }
    /* the gun's lead: where to put the nose for the rounds (1150 m/s, plus our own speed) to meet the nearest
     * enemy inside gun range; the bare gunsight gave no lead, so a crossing shot was guesswork */
    if (nearest && nearest->alive && !padlock && plane_has_gun(p) && v3_dist(nearest->pos, p->pos) < 1800) {
      /* the time t when a round can be where the enemy will be: |rel + w t| = 1150 t, w the enemy's velocity
       * less ours (the round carries our velocity) */
      vec3 rel = v3_sub(nearest->pos, p->pos), w = v3_sub(nearest->vel, p->vel);
      float qa = v3_dot(w, w) - 1150.0f * 1150.0f, qb = 2 * v3_dot(rel, w), qc = v3_dot(rel, rel);
      float t = (-qb - sqrtf(fmaxf(0, qb * qb - 4 * qa * qc))) / (2 * qa); /* qa < 0: the one positive root */
      vec3 aim = v3_add(rel, v3_scale(w, t));
      vec3 c = hud_cam(V, v3_add(p->pos, aim));
      if (c.z > NEAR_Z) {
        float lx = cx + c.x / c.z * V->F, ly = cy - c.y / c.z * V->F;
        if (lx > vx && lx < vx + vw && ly > vy && ly < vy + sh) {
          fb_ring(fb, lx, ly, 5 * k, C(HUDC), k);
          fb_rect(fb, (int)(lx - k / 2), (int)(ly - k / 2), ks, ks, C(HUDC));
          M->lead = true;
          M->leadx = lx;
          M->leady = ly;
        }
      }
    }
    if (g->def.on) {
      const Defense *d = &g->def;
      snprintf(buf, sizeof buf, "CARRIER %d%%", (int)fmaxf(0, d->hp / d->hpmax * 100));
      fb_text(fb, buf, vx + vw - 6 * k, vy + 8 * k, ks, C(d->hp < d->hpmax * 0.35f ? WARN : HUDC), ALIGN_RIGHT, 2);
      if (d->between) snprintf(buf, sizeof buf, "WAVE %d IN %d", d->wave + 1, (int)ceilf(d->waveT));
      else snprintf(buf, sizeof buf, "WAVE %d/%d", d->wave, d->waves);
      fb_text(fb, buf, vx + vw - 6 * k, vy + 18 * k, ks, C(INK), ALIGN_RIGHT, 2);
      /* inbound anti-ship missiles */
      for (int i = 0; i < g->nmissiles; i++) {
        const Missile *m = &g->missiles[i];
        if (!m->ashm) continue;
        vec3 c = hud_cam(V, m->pos);
        if (c.z < NEAR_Z) continue;
        float sx = cx + c.x / c.z * V->F, sy = cy - c.y / c.z * V->F;
        if (sx < vx || sx > vx + vw || sy < vy || sy > vy + sh) continue;
        fb_line(fb, sx - 4 * k, sy - 4 * k, sx, sy + 3 * k, C(AMBER), k, 1);
        fb_line(fb, sx, sy + 3 * k, sx + 4 * k, sy - 4 * k, C(AMBER), k, 1);
        snprintf(buf, sizeof buf, "%.1fKM", v3_dist(m->pos, p->pos) / 1000);
        fb_text(fb, buf, sx, sy + 9 * k, ks, C(AMBER), ALIGN_CENTER, 2);
      }
    } else if (g->coop || g->nplanes > 2) {
      int n = 0;
      for (int i = 0; i < g->nplanes; i++)
        if (g->planes[i].team != p->team && g->planes[i].lives > 0) n += g->planes[i].lives;
      snprintf(buf, sizeof buf, "ENEMY JETS %d", n);
      fb_text(fb, buf, vx + vw - 6 * k, vy + 8 * k, ks, C(TEAM[1 - p->team].ui), ALIGN_RIGHT, 2);
    }
    /* the other side's tanker, a target: marked in their colour */
    const Tanker *ET = &g->tankers[!(p->team & 1)];
    vec3 ec = hud_cam(V, ET->pos);
    if (ec.z > 200 && !ET->gone) {
      float sx = cx + ec.x / ec.z * V->F, sy = cy - ec.y / ec.z * V->F;
      if (sx > vx && sx < vx + vw && sy > vy && sy < vy + sh) {
        snprintf(buf, sizeof buf, "TANKER %.1fKM", ec.z / 1000);
        float lx = label_x(buf, sx, vx, vw, ks), ly = label_clear(buf, lx, label_y(sy, vy, sh, k), vy, sh, ks, lab, nlab);
        fb_text(fb, buf, lx, ly, ks, C(TEAM[1 - p->team].ui), ALIGN_CENTER, 2);
        keep_clear(lab, &nlab, buf, lx, ly, ks);
      }
    }
    /* tanker marker */
    const Tanker *OT = &g->tankers[p->team & 1];
    vec3 tc = hud_cam(V, OT->pos);
    if (tc.z > 200 && !OT->gone) {
      float sx = cx + tc.x / tc.z * V->F, sy = cy - tc.y / tc.z * V->F;
      if (sx > vx && sx < vx + vw && sy > vy && sy < vy + sh) {
        M->tanker = true;
        M->tankerKm = tc.z / 1000;
        if (p->fuel < 35) snprintf(buf, sizeof buf, "TANKER %.1fKM", tc.z / 1000);
        else snprintf(buf, sizeof buf, "T");
        float lx = label_x(buf, sx, vx, vw, ks), ly = label_clear(buf, lx, label_y(sy, vy, sh, k), vy, sh, ks, lab, nlab);
        fb_text(fb, buf, lx, ly, ks, C(0xf0f0f0), ALIGN_CENTER, 2);
        keep_clear(lab, &nlab, buf, lx, ly, ks);
      }
    }
    /* lock diamond */
    vec3 lp;
    if (p->lockTarget.kind != REF_NONE && ref_get(g, p->lockTarget, &lp, NULL)) {
      vec3 c = hud_cam(V, lp);
      if (c.z > NEAR_Z) {
        float sx = cx + c.x / c.z * V->F, sy = cy - c.y / c.z * V->F;
        float s = (p->locked ? 7 : lerpf(28, 9, fminf(1, p->lockT / LOCK_TIME))) * k;
        M->lock = p->locked;
        M->lx = sx;
        M->ly = sy;
        uint32_t col = C(p->locked ? WARN : AMBER);
        fb_line(fb, sx, sy - s, sx + s, sy, col, k, 1);
        fb_line(fb, sx + s, sy, sx, sy + s, col, k, 1);
        fb_line(fb, sx, sy + s, sx - s, sy, col, k, 1);
        fb_line(fb, sx - s, sy, sx, sy - s, col, k, 1);
        if (p->locked) fb_text(fb, "LOCK", sx, sy - s - 6 * k, ks, col, ALIGN_CENTER, 2);
        if (p->locked && nlab < 12) { /* the messages keep clear of it too: "FAST - WIDE TURN" was written over LOCK */
          float hw = fmaxf(s, text_width("LOCK", ks) / 2.0f) + 2 * k;
          lab[nlab][0] = sx - hw, lab[nlab][1] = sy - s - 12 * k, lab[nlab][2] = sx + hw, lab[nlab][3] = sy + s;
          nlab++;
        }
      }
    }
    /* status messages */
    const char *msg[8];
    uint32_t mcol[8];
    char refuel[32], aoa[32], bingo[32];
    int nm = 0;
#define MSG(s, c) (msg[nm] = (s), mcol[nm] = (c), nm++)
    if (p->warn && blink) MSG("MISSILE!", WARN);
    float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
    /* the ground coming up: under 6 s to impact at this sink rate, or low and nose down (it used to wait for
     * 300 m and the nose 9 degrees down, too late at speed); not on the approach to our own runway */
    const Base *hb = &BASES[p->team];
    bool approach = fabsf(p->pos.x - hb->x) < 80 && fabsf(p->pos.z - hb->z) < 4500 && p->speed < 150;
    float sink = -p->vel.y;
    bool closing = sink > 3 && agl < 2500 && agl < sink * 6;
    if (!p->onGround && !p->taxi && !p->refuel && !approach && (closing || (agl < 300 && p->f.y < -0.15f)) && blink)
      MSG("PULL UP", WARN);
    /* the drone: the nozzle at work, and how far the nose is off the path */
    if (p->drone && (p->vecOn || p->aoa > 0.35f)) {
      snprintf(aoa, sizeof aoa, "%s  AOA %d", p->vecOn ? "VECTOR" : "POST-STALL", (int)roundf(p->aoa * 180 / PI_F));
      MSG(aoa, p->aoa > 0.6f ? AMBER : HUDC);
    }
    if (p->stall) MSG("STALL", AMBER);
    /* before the stall, not after it: the wing's warning a real jet gives with buffet */
    else if (!p->onGround && !p->taxi && !p->drone && p->speed < (g->classic ? 72 : 90) * 1.2f) MSG("SPEED LOW", AMBER);
    /* in a hard turn: where the speed is against corner speed, the fastest turn */
    else if (!p->onGround && p->gload > 3.5f) {
      float vc = jet_corner(jet_type(p->jet, g->realistic));
      MSG(p->speed < vc * 0.85f ? "SLOW - TURN WEAKENING" : p->speed > vc * 1.2f ? "FAST - WIDE TURN" : "CORNER SPEED", HINT); /* (DIM: the haze's colour) */
    }
    if (p->out) MSG("RETURN TO COMBAT AREA", AMBER);
    if (p->fuel <= 0) MSG("FLAMEOUT", WARN);
    else if (p->fuel < 20 && !p->refuel && blink) MSG("LOW FUEL", AMBER);
    else if (!p->refuel && !p->onGround && g->time < TANKER_STAYS && p->fuel < game_bingo(g, p)) {
      /* bingo: just enough to get home, round the circuit and down */
      snprintf(bingo, sizeof bingo, "BINGO - BASE %.0f KM", hypotf(p->pos.x - hb->x, p->pos.z - hb->z) / 1000);
      MSG(bingo, AMBER);
    }
    if (p->refuel) {
      snprintf(refuel, sizeof refuel, "REFUELLING %d%%", (int)roundf(p->fuel));
      MSG(refuel, HUDC);
    }
    /* realistic: low, slow and coming down with the gear not down, the warning a jet gives */
    if (g->realistic && !p->drone && !p->onGround && !p->taxi && p->gearT < 1 && p->speed < 115 && p->vel.y < 0 &&
        p->pos.y - terrain_h(p->pos.x, p->pos.z) < 300 && blink)
      MSG("GEAR NOT DOWN", WARN);
    if (p->taxi && p->oncat && p->cat <= 0) MSG(g->classic ? "FULL POWER (D) TO LAUNCH" : "FULL POWER (R) TO LAUNCH", HUDC);
    else if (p->taxi && p->oncat) MSG("CATAPULT", HUDC);
    else if (p->taxi && !p->oncat) MSG(p->speed < (g->classic ? 80 : 92) ? "TAKE-OFF ROLL - POWER UP" : "ROTATE - PULL BACK", HUDC);
    else if (p->onGround && !p->refuel) MSG("ON RUNWAY", HUDC);
    if (p->autopilot) MSG("AUTOPILOT", HUDC);
#undef MSG
    /* the messages under the sight, moved down (or up, at the bottom) past any range label they would cover */
    float my = cy + sh * 0.2f, lh = 11 * k, half = 6 * k;
    for (int tries = 0; tries < 12; tries++) {
      bool hit = false;
      for (int i = 0; i < nm && i < 3 && !hit; i++) {
        float y = my + i * lh, hw = text_width(msg[i], ks) / 2.0f + 2 * k;
        for (int j = 0; j < nlab && !hit; j++)
          hit = cx + hw > lab[j][0] && cx - hw < lab[j][2] && y + half > lab[j][1] && y - half < lab[j][3];
      }
      if (!hit) break;
      my += tries < 6 ? lh : -lh * (tries == 6 ? 7 : 1);
    }
    for (int i = 0; i < nm && i < 3; i++) fb_text(fb, msg[i], cx, my + i * lh, ks, C(mcol[i]), ALIGN_CENTER, 2);
    if (p->evT > 0) {
      /* a change of weapon in big letters, so it is clear what is armed now */
      static const char *const BIG[] = { "CANNON", "HEAT-SEEKER", "RADAR-GUIDED", "AIR-TO-GROUND", "RADAR MISSILES NEXT" };
      bool big = false;
      for (size_t i = 0; i < sizeof BIG / sizeof BIG[0]; i++) big = big || !strcmp(p->ev, BIG[i]);
      fb_text(fb, p->ev, cx, cy - sh * 0.22f, big ? 2 * ks : ks, C(big ? HUDC : INK), ALIGN_CENTER, 2);
    }
    if (g->time < 6 && !g->over && p->drone)
      fb_text(fb, p->hidx == 1 ? "HOLD Y: VECTOR  PULL: COBRA, FLIP" : "HOLD T OR E: VECTOR  PULL: COBRA, FLIP",
              cx, vy + sh - 8 * k, ks, C(HINT), ALIGN_CENTER, 2);
    else if (g->time < 6 && !g->over) fb_text(fb, "TAB  CHASE VIEW     ESC  PAUSE", cx, vy + sh - 8 * k, ks, C(HINT), ALIGN_CENTER, 2);
  } else {
    fb_text(fb, p->deathMsg ? p->deathMsg : "", cx, cy - 12 * k, 2 * ks, C(WARN), ALIGN_CENTER, 2);
    if (p->lives > 0) {
      if (!g->over) snprintf(buf, sizeof buf, "NEXT JET IN %d", (int)fmaxf(1, ceilf(p->respawnT)));
      else buf[0] = 0;
    } else snprintf(buf, sizeof buf, "NO JETS LEFT");
    fb_text(fb, buf, cx, cy + 8 * k, ks, C(INK), ALIGN_CENTER, 2);
  }
  if (p->hitFlash > 0) fb_rect_a(fb, (int)vx, (int)vy, (int)vw, (int)sh, rgb(255, 40, 30), p->hitFlash * 0.3f);
  if (p->alive && p->invuln > 0 && blink) fb_text(fb, "READY", cx, cy - sh * 0.3f, ks, C(HUDC), ALIGN_CENTER, 2);
}
