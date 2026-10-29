/* hud.c - gunsight, heading tape, jet markers, lock diamond, warnings,
 * cockpit panel, radar scope and attitude ball (JS drawHUD, drawPanel, bar,
 * lamp, radarScope, attitude; lines 3487-3660). */
#include "hud.h"

#include <stdio.h>

static inline uint32_t C(uint32_t c) { return hexc(c); }

static vec3 hud_cam(const HudView *V, vec3 w) {
  vec3 d = v3_sub(w, V->pos);
  return v3(v3_dot(d, V->r), v3_dot(d, V->u), v3_dot(d, V->f));
}

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

void hud_draw(Framebuffer *fb, Game *g, Plane *p, const HudView *V, bool label) {
  const float vx = V->vx, vy = V->vy, vw = V->vw, sh = V->sh, cx = V->cx, cy = V->cy;
  int ks = (int)fmaxf(1, floorf(fminf(vw / 300, sh / 150)));
  const float k = (float)ks;
  bool blink = ((int)(g->time * 4)) % 2 == 0;
  char buf[64];

  if (p->alive && g->g_effects) g_vision(fb, p, V);
  if (label) fb_text(fb, p->name, vx + 6 * k, vy + 8 * k, ks, C(TEAM[p->team].ui), ALIGN_LEFT, 1);
  if (p->alive && p->gloc > 0) {
    if (blink) fb_text(fb, p->gneg > p->gstress ? "REDOUT" : "G-LOC", cx, cy, 2 * ks, C(WARN), ALIGN_CENTER, 1);
  } else if (p->alive) {
    /* G meter */
    snprintf(buf, sizeof buf, "G %+.1f", p->gload);
    uint32_t gc = p->gload > 8.5f || p->gload < -2 ? WARN : p->gload > 7 ? AMBER : HUDC;
    fb_text(fb, buf, vx + 8 * k, cy, ks, C(gc), ALIGN_LEFT, 1);
    /* gunsight */
    float gp = 4 * k, l = 9 * k;
    fb_line(fb, cx - l, cy, cx - gp, cy, C(HUDC), k, 1);
    fb_line(fb, cx + gp, cy, cx + l, cy, C(HUDC), k, 1);
    fb_line(fb, cx, cy - l, cx, cy - gp, C(HUDC), k, 1);
    fb_line(fb, cx, cy + gp, cx, cy + l, C(HUDC), k, 1);
    fb_rect(fb, (int)(cx - k / 2), (int)(cy - k / 2), ks, ks, C(HUDC));

    /* heading tape */
    float hdg = fmodf(atan2f(p->f.x, p->f.z) * 180 / PI_F + 360, 360);
    float tw = fminf(vw * 0.5f, 160 * k), ty = vy + 8 * k;
    for (int d = (int)ceilf((hdg - 30) / 5) * 5; d <= hdg + 30; d += 5) {
      float x = cx + (d - hdg) / 30 * tw / 2;
      fb_line(fb, x, ty + 6 * k, x, ty + (d % 10 == 0 ? 2 : 4) * k, C(HUDC), k, 1);
      int dd = ((d % 360) + 360) % 360;
      if (dd % 90 == 0) {
        char s[2] = { "NESW"[dd / 90], 0 };
        fb_text(fb, s, x, ty - 2 * k, ks, C(HUDC), ALIGN_CENTER, 0);
      }
    }
    snprintf(buf, sizeof buf, "%03d", ((int)roundf(hdg)) % 360);
    fb_text(fb, buf, cx, ty + 13 * k, ks, C(HUDC), ALIGN_CENTER, 1);

    /* jet markers: brackets for enemies, a tag for wingmen, an edge arrow for the nearest enemy */
    Plane *nearest = game_enemy_of(g, p->team, p->pos);
    for (int i = 0; i < g->nplanes; i++) {
      Plane *e = &g->planes[i];
      if (e == p || !e->alive) continue;
      bool foe = e->team != p->team;
      vec3 c = hud_cam(V, e->pos);
      float d = v3_dist(e->pos, p->pos);
      uint32_t col = C(TEAM[e->team].ui);
      if (foe) snprintf(buf, sizeof buf, "%.1fKM", d / 1000);
      else snprintf(buf, sizeof buf, "%s", e->name);
      float sx = c.z > NEAR_Z ? cx + c.x / c.z * V->F : 0, sy = c.z > NEAR_Z ? cy - c.y / c.z * V->F : 0;
      float m = 14 * k;
      if (!foe) {
        if (c.z > NEAR_Z && sx > vx && sx < vx + vw && sy > vy && sy < vy + sh) {
          fb_rect_stroke(fb, sx - 3 * k, sy - 3 * k, 6 * k, 6 * k, C(HUDC), k);
          fb_text(fb, buf, sx, sy - 8 * k, ks, C(HUDC), ALIGN_CENTER, 1);
        }
        continue;
      }
      bool onScreen = c.z > NEAR_Z && sx > vx + m && sx < vx + vw - m && sy > vy + m && sy < vy + sh - m;
      if (e != nearest && !onScreen) continue;
      if (onScreen) {
        float s = fmaxf(6 * k, 9 / c.z * V->F);
        static const int A[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int q = 0; q < 4; q++) {
          float ax = (float)A[q][0], ay = (float)A[q][1];
          float px = sx + ax * s, py = sy + ay * s;
          fb_line(fb, px, py - ay * s * 0.45f, px, py, col, k, 1);
          fb_line(fb, px, py, px - ax * s * 0.45f, py, col, k, 1);
        }
        fb_text(fb, buf, sx, sy + s + 6 * k, ks, col, ALIGN_CENTER, 1);
      } else {
        float ang = atan2f(-c.y, c.x);
        float ex = cx + cosf(ang) * (vw / 2 - m), ey = cy + sinf(ang) * (sh / 2 - m);
        tri(fb, ex + cosf(ang) * 7 * k, ey + sinf(ang) * 7 * k, ex + cosf(ang + 2.5f) * 6 * k,
            ey + sinf(ang + 2.5f) * 6 * k, ex + cosf(ang - 2.5f) * 6 * k, ey + sinf(ang - 2.5f) * 6 * k, col);
        fb_text(fb, buf, ex - cosf(ang) * 14 * k, ey - sinf(ang) * 10 * k, ks, col, ALIGN_CENTER, 1);
      }
    }
    if (g->def.on) {
      const Defense *d = &g->def;
      snprintf(buf, sizeof buf, "CARRIER %d%%", (int)fmaxf(0, d->hp / d->hpmax * 100));
      fb_text(fb, buf, vx + vw - 6 * k, vy + 8 * k, ks, C(d->hp < d->hpmax * 0.35f ? WARN : HUDC), ALIGN_RIGHT, 1);
      if (d->between) snprintf(buf, sizeof buf, "WAVE %d IN %d", d->wave + 1, (int)ceilf(d->waveT));
      else snprintf(buf, sizeof buf, "WAVE %d/%d", d->wave, d->waves);
      fb_text(fb, buf, vx + vw - 6 * k, vy + 18 * k, ks, C(INK), ALIGN_RIGHT, 1);
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
        fb_text(fb, buf, sx, sy + 9 * k, ks, C(AMBER), ALIGN_CENTER, 1);
      }
    } else if (g->coop || g->nplanes > 2) {
      int n = 0;
      for (int i = 0; i < g->nplanes; i++)
        if (g->planes[i].team != p->team && g->planes[i].lives > 0) n += g->planes[i].lives;
      snprintf(buf, sizeof buf, "ENEMY JETS %d", n);
      fb_text(fb, buf, vx + vw - 6 * k, vy + 8 * k, ks, C(TEAM[1 - p->team].ui), ALIGN_RIGHT, 1);
    }
    /* tanker marker */
    vec3 tc = hud_cam(V, g->tanker.pos);
    if (tc.z > 200) {
      float sx = cx + tc.x / tc.z * V->F, sy = cy - tc.y / tc.z * V->F;
      if (sx > vx && sx < vx + vw && sy > vy && sy < vy + sh) {
        if (p->fuel < 35) snprintf(buf, sizeof buf, "TANKER %.1fKM", tc.z / 1000);
        else snprintf(buf, sizeof buf, "T");
        fb_text(fb, buf, sx, sy - 8 * k, ks, C(0xf0f0f0), ALIGN_CENTER, 1);
      }
    }
    /* lock diamond */
    vec3 lp;
    if (p->lockTarget.kind != REF_NONE && ref_get(g, p->lockTarget, &lp, NULL)) {
      vec3 c = hud_cam(V, lp);
      if (c.z > NEAR_Z) {
        float sx = cx + c.x / c.z * V->F, sy = cy - c.y / c.z * V->F;
        float s = (p->locked ? 7 : lerpf(28, 9, fminf(1, p->lockT / LOCK_TIME))) * k;
        uint32_t col = C(p->locked ? WARN : AMBER);
        fb_line(fb, sx, sy - s, sx + s, sy, col, k, 1);
        fb_line(fb, sx + s, sy, sx, sy + s, col, k, 1);
        fb_line(fb, sx, sy + s, sx - s, sy, col, k, 1);
        fb_line(fb, sx - s, sy, sx, sy - s, col, k, 1);
        if (p->locked) fb_text(fb, "LOCK", sx, sy - s - 6 * k, ks, col, ALIGN_CENTER, 1);
      }
    }
    /* status messages */
    const char *msg[8];
    uint32_t mcol[8];
    char refuel[32];
    int nm = 0;
#define MSG(s, c) (msg[nm] = (s), mcol[nm] = (c), nm++)
    if (p->warn && blink) MSG("MISSILE!", WARN);
    float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
    if (!p->onGround && !p->refuel && agl < 300 && p->f.y < -0.15f && blink) MSG("PULL UP", WARN);
    if (p->stall) MSG("STALL", AMBER);
    /* in a hard turn: where the speed is against corner speed, the fastest turn */
    else if (!p->onGround && p->gload > 3.5f) {
      float vc = jet_corner(&JETS[p->jet]);
      MSG(p->speed < vc * 0.85f ? "SLOW - TURN WEAKENING" : p->speed > vc * 1.2f ? "FAST - WIDE TURN" : "CORNER SPEED", DIM);
    }
    if (p->out) MSG("RETURN TO COMBAT AREA", AMBER);
    if (p->fuel <= 0) MSG("FLAMEOUT", WARN);
    else if (p->fuel < 20 && !p->refuel && blink) MSG("LOW FUEL", AMBER);
    if (p->refuel) {
      snprintf(refuel, sizeof refuel, "REFUELLING %d%%", (int)roundf(p->fuel));
      MSG(refuel, HUDC);
    }
    if (p->taxi && p->oncat && p->cat <= 0) MSG(g->classic ? "FULL POWER (D) TO LAUNCH" : "FULL POWER (R) TO LAUNCH", HUDC);
    else if (p->taxi && p->oncat) MSG("CATAPULT", HUDC);
    else if (p->taxi && !p->oncat) MSG(p->speed < (g->classic ? 80 : 92) ? "TAKE-OFF ROLL - POWER UP" : "ROTATE - PULL BACK", HUDC);
    else if (p->onGround && !p->refuel) MSG("ON RUNWAY", HUDC);
    if (p->autopilot) MSG("AUTOPILOT", HUDC);
#undef MSG
    for (int i = 0; i < nm && i < 3; i++) fb_text(fb, msg[i], cx, cy + sh * 0.2f + i * 11 * k, ks, C(mcol[i]), ALIGN_CENTER, 1);
    if (p->evT > 0) fb_text(fb, p->ev, cx, cy - sh * 0.22f, ks, C(INK), ALIGN_CENTER, 1);
    if (g->time < 6 && !g->over) fb_text(fb, "TAB  CHASE VIEW     ESC  PAUSE", cx, vy + sh - 8 * k, ks, C(DIM), ALIGN_CENTER, 1);
  } else {
    fb_text(fb, p->deathMsg ? p->deathMsg : "", cx, cy - 12 * k, 2 * ks, C(WARN), ALIGN_CENTER, 1);
    if (p->lives > 0) {
      if (!g->over) snprintf(buf, sizeof buf, "NEXT JET IN %d", (int)fmaxf(1, ceilf(p->respawnT)));
      else buf[0] = 0;
    } else snprintf(buf, sizeof buf, "NO JETS LEFT");
    fb_text(fb, buf, cx, cy + 8 * k, ks, C(INK), ALIGN_CENTER, 1);
  }
  if (p->hitFlash > 0) fb_rect_a(fb, (int)vx, (int)vy, (int)vw, (int)sh, rgb(255, 40, 30), p->hitFlash * 0.3f);
  if (p->alive && p->invuln > 0 && blink) fb_text(fb, "READY", cx, cy - sh * 0.3f, ks, C(HUDC), ALIGN_CENTER, 1);
}
