/* command.c - player 2's seat in the carrier defence. In the control room
 * the left side is a tactical radar centred on the carrier (land, range
 * rings, bogeys, inbound missiles, our jets, turret orders) and the right
 * side the turret board. Player 2 picks a turret and a contact and orders
 * the turret onto it, switches turrets between AUTO and HOLD, mans one to
 * aim and fire it by hand, or launches from the catapult and flies. */
#include "command.h"

#include <stdio.h>
#include <string.h>

#include "core/defend.h"
#include "core/world.h"
#include "hud.h"
#include "render3d.h"

static inline uint32_t C(uint32_t c) { return hexc(c); }

bool command_active(const Game *g) { return g->def.on && g->def.commander && g->def.cmd_state != CMD_FLYING; }

/* The captain's own controls on foot are read every tick (input.c);
 * here are the presses: use, back, and the tactical console's keys. */

static void contact_label(const Game *g, Ref r, char *buf, size_t n) {
  if (r.kind == REF_PLANE) {
    const char *nm = g->planes[r.idx].name;
    const char *num = strchr(nm, ' ');
    snprintf(buf, n, "B%s", num ? num + 1 : "");
  } else if (r.kind == REF_MISSILE) snprintf(buf, n, "VAMPIRE");
  else snprintf(buf, n, "-");
}

/* the same contact: a missile by its id (its slot in g->missiles changes as the list closes up), anything else as
 * ref_eq has it */
static bool same_contact(Ref a, Ref b) {
  if (a.kind == REF_MISSILE || b.kind == REF_MISSILE) return a.kind == b.kind && a.gen == b.gen;
  return ref_eq(a, b);
}

void command_action(Game *g, CmdAction a) {
  Defense *d = &g->def;
  if (!command_active(g)) return;
  Turret *t = &d->tur[d->sel];
  if (d->cmd_state == CMD_TURRET) {
    if (a == CA_MAN) { /* step back from the gun */
      t->mode = TM_AUTO;
      t->man_fire = false;
      d->cmd_state = CMD_WALK;
    }
    return;
  }
  if (d->cmd_state == CMD_WALK) {
    if (a == CA_ORDER) defend_use(g);
    return;
  }
  if (a == CA_MAN) { /* leave the console */
    d->cmd_state = CMD_WALK;
    return;
  }
  Ref list[MAX_PLANES + MAX_MISSILES];
  int n = defend_contacts(g, list, MAX_PLANES + MAX_MISSILES), at = -1;
  for (int i = 0; i < n; i++)
    if (same_contact(list[i], d->pick)) at = i;
  switch (a) {
  case CA_UP:    d->sel = (d->sel + d->nturrets - 1) % d->nturrets; break;
  case CA_DOWN:  d->sel = (d->sel + 1) % d->nturrets; break;
  case CA_LEFT:
  case CA_RIGHT:
    if (n) d->pick = list[((at < 0 ? 0 : at + (a == CA_RIGHT ? 1 : -1)) + n) % n];
    break;
  case CA_ORDER:
    if (d->pick.kind != REF_NONE && ref_get(g, d->pick, NULL, NULL)) {
      t->target = d->pick;
      t->mode = TM_ASSIGNED;
    } else if (n) {
      d->pick = list[0];
    }
    break;
  case CA_MODE:   t->mode = t->mode == TM_HOLD ? TM_AUTO : TM_HOLD; break;
  case CA_LAUNCH: defend_cmd_launch(g); break;
  default: break;
  }
}

/* ---- the control room ---------------------------------------------------- */
/* A modern combat-information layout: a header bar, the tactical display
 * on the left and glass cards on the right (ship, turrets, contact). */
#define CI_BG    0x070b12
#define CI_CARD  0x0d1520
#define CI_EDGE  0x1d2c3c
#define CI_HEAD  0x13202e
#define CI_CYAN  0x4fd8ff
#define CI_TEXT  0xd8e6f2
#define CI_DIM   0x6f8496
#define CI_RED   0xff4a3c
#define CI_GREEN 0x5cff8a

static void card(Framebuffer *fb, float x, float y, float w, float h, const char *title, int ks) {
  float k = (float)ks;
  fb_rect(fb, (int)x, (int)y, (int)w, (int)h, C(CI_CARD));
  fb_rect_stroke(fb, x, y, w, h, C(CI_EDGE), 1);
  fb_rect(fb, (int)x, (int)y, (int)w, (int)(11 * k), C(CI_HEAD));
  fb_rect(fb, (int)x, (int)y, (int)fmaxf(2, 2 * k), (int)(11 * k), C(CI_CYAN));
  fb_text(fb, title, x + 6 * k, y + 5.5f * k, ks, C(CI_CYAN), ALIGN_LEFT, 0);
}

/* A coloured label in a filled pill; returns its width. */
static float chip(Framebuffer *fb, const char *s, float x, float y, uint32_t col, bool filled, int ks, int align) {
  float k = (float)ks, w = (float)text_width(s, ks) + 8 * k, h = 10 * k;
  if (align == ALIGN_RIGHT) x -= w;
  if (filled) fb_rect(fb, (int)x, (int)(y - h / 2), (int)w, (int)h, C(col));
  else fb_rect_stroke(fb, x, y - h / 2, w, h, C(col), 1);
  fb_text(fb, s, x + w / 2, y, ks, filled ? C(0x081018) : C(col), ALIGN_CENTER, 0);
  return w;
}

static void meter(Framebuffer *fb, float x, float y, float w, float h, float v, uint32_t col) {
  fb_rect(fb, (int)x, (int)y, (int)w, (int)h, C(0x1a2634));
  if (v > 0) fb_rect(fb, (int)x, (int)y, (int)fmaxf(1, w * fminf(1, v)), (int)h, C(col));
}

static void room(Framebuffer *fb, Game *g, int vx, int vy, int vw, int vh) {
  Defense *d = &g->def;
  char buf[64];
  fb_rect(fb, vx, vy, vw, vh, C(CI_BG));
  int ks = vw >= 560 && vh >= 300 ? 2 : 1;
  float k = (float)ks;
  bool blink = ((int)(g->time * 4)) % 2;
  int bogeys = 0, vampires = 0;
  for (int i = 0; i < g->nplanes; i++) bogeys += g->planes[i].team == 1 && !g->planes[i].inactive && g->planes[i].alive;
  for (int i = 0; i < g->nmissiles; i++) vampires += g->missiles[i].ashm;

  /* header */
  float hh = 14 * k;
  fb_rect(fb, vx, vy, vw, (int)hh, C(CI_HEAD));
  fb_rect(fb, vx, (int)(vy + hh), vw, 1, C(CI_EDGE));
  fb_text(fb, "CVN-90 STORMHAVEN  CIC", vx + 6 * k, vy + hh / 2, ks, C(CI_TEXT), ALIGN_LEFT, 0);
  if (d->between) snprintf(buf, sizeof buf, "WAVE %d IN %d", d->wave + 1, (int)ceilf(d->waveT));
  else snprintf(buf, sizeof buf, "WAVE %d/%d", d->wave, d->waves);
  float cxr = vx + vw - 6 * k;
  cxr -= chip(fb, buf, cxr, vy + hh / 2, d->between ? 0xffb84a : CI_CYAN, false, ks, ALIGN_RIGHT) + 4 * k;
  if (vampires) chip(fb, "VAMPIRE", cxr, vy + hh / 2, CI_RED, blink, ks, ALIGN_RIGHT);

  /* tactical display */
  float top = vy + hh + 4 * k, avail = vh - hh - 8 * k;
  float side = fminf(vw * 0.58f, avail);
  float px = vx + 4 * k, py = top;
  card(fb, px, py, side, avail, "TACTICAL  12 KM", ks);
  float R = fminf(side - 8 * k, avail - 16 * k) / 2, cx = px + side / 2, cy = py + 12 * k + (avail - 12 * k) / 2;
  const float RANGE = 12000, sc = R / RANGE;
  fb_circle(fb, cx, cy, R, C(0x061018));
  fb_clip_circle(fb, cx, cy, R);
  if (MAP) {
    /* land as dim cells */
    float cell = MAP->spacing, cs = fmaxf(1, cell * sc);
    for (int j = 0; j < MAP_CELLS; j++)
      for (int i = 0; i < MAP_CELLS; i++) {
        int mi = MAP->mat[j * MAP_CELLS + i];
        if (mi < MAP->npal && MAP->pal[mi].water) continue;
        float wx = MAP->origin_x + (i + 0.5f) * cell - d->pos.x, wz = MAP->origin_z + (j + 0.5f) * cell - d->pos.z;
        if (fabsf(wx) > RANGE || fabsf(wz) > RANGE) continue;
        fb_rect(fb, (int)(cx + wx * sc - cs / 2), (int)(cy - wz * sc - cs / 2), (int)ceilf(cs), (int)ceilf(cs), C(0x163340));
      }
  }
  for (int r = 3; r <= 12; r += 3) fb_ring(fb, cx, cy, r * 1000 * sc, C(0x173444), 1);
  for (int a = 0; a < 12; a++) {
    float an = a * PI_F / 6;
    fb_line(fb, cx + sinf(an) * R * 0.25f, cy - cosf(an) * R * 0.25f, cx + sinf(an) * R, cy - cosf(an) * R, C(0x10242f), 1, 1);
  }
  /* defence rings: CIWS reach and SAM reach */
  fb_ring(fb, cx, cy, 2400 * sc, C(0x2a6a7a), 1);
  fb_ring(fb, cx, cy, 7500 * sc, C(0x1f5060), 1);
  /* sweep */
  float sw = fmodf(g->time * 1.6f, 6.2831853f);
  for (int i = 0; i < 12; i++) {
    float a = sw - i * 0.035f;
    fb_line(fb, cx, cy, cx + sinf(a) * R, cy - cosf(a) * R, C(CI_CYAN), 1, 0.3f - i * 0.024f);
  }
#define TP(w, ox, oy) (ox = cx + ((w).x - d->pos.x) * sc, oy = cy - ((w).z - d->pos.z) * sc)
  for (int t = 0; t < d->nturrets; t++) {
    vec3 tp;
    if (d->tur[t].mode == TM_HOLD || !ref_get(g, d->tur[t].target, &tp, NULL)) continue;
    float x, y;
    TP(tp, x, y);
    fb_line(fb, cx, cy, x, y, C(t == d->sel ? 0xffd25a : 0x7a6a30), 1, t == d->sel ? 0.9f : 0.5f);
  }
  for (int i = 0; i < g->nmissiles; i++) {
    const Missile *m = &g->missiles[i];
    float x, y;
    TP(m->pos, x, y);
    fb_rect(fb, (int)x - ks, (int)y - ks, 2 * ks, 2 * ks, C(m->team == 0 ? 0xf0f0f0 : 0xffd25a));
  }
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *p = &g->planes[i];
    if (!p->alive || p->inactive) continue;
    float x, y, h = atan2f(p->f.x, p->f.z), s = 4 * k;
    TP(p->pos, x, y);
    uint32_t col = p->team ? C(CI_RED) : C(p->hidx == 1 ? 0x7af0ff : 0x6aa8ff);
    if (p->team) {
      /* hostile: a red diamond with a speed leader */
      float xy[8] = { x, y - s, x + s, y, x, y + s, x - s, y };
      fb_poly(fb, xy, 4, col, 1, 0);
      fb_line(fb, x, y, x + sinf(h) * s * 3, y - cosf(h) * s * 3, col, 1, 1);
      contact_label(g, ref_plane(i), buf, sizeof buf);
      fb_text(fb, buf, x + 6 * k, y - 5 * k, ks, col, ALIGN_LEFT, 0);
    } else {
      float xy[6] = { x + sinf(h) * s, y - cosf(h) * s, x + sinf(h + 2.5f) * s, y - cosf(h + 2.5f) * s, x + sinf(h - 2.5f) * s, y - cosf(h - 2.5f) * s };
      fb_poly(fb, xy, 3, col, 1, 0);
    }
  }
  vec3 pp;
  if (ref_get(g, d->pick, &pp, NULL)) {
    float x, y, b = 7 * k, l = 3 * k;
    TP(pp, x, y);
    uint32_t w = C(0xffffff);
    /* corner brackets */
    fb_line(fb, x - b, y - b, x - b + l, y - b, w, 1, 1); fb_line(fb, x - b, y - b, x - b, y - b + l, w, 1, 1);
    fb_line(fb, x + b, y - b, x + b - l, y - b, w, 1, 1); fb_line(fb, x + b, y - b, x + b, y - b + l, w, 1, 1);
    fb_line(fb, x - b, y + b, x - b + l, y + b, w, 1, 1); fb_line(fb, x - b, y + b, x - b, y + b - l, w, 1, 1);
    fb_line(fb, x + b, y + b, x + b - l, y + b, w, 1, 1); fb_line(fb, x + b, y + b, x + b, y + b - l, w, 1, 1);
  }
  {
    float h = d->heading * PI_F / 180, L = fmaxf(3, d->length / 2 * sc), B = fmaxf(2, d->beam / 2 * sc);
    float fx = sinf(h), fz = cosf(h), rx = cosf(h), rz = -sinf(h);
    float xy[8] = { cx + fx * L + rx * B, cy - (fz * L + rz * B), cx + fx * L - rx * B, cy - (fz * L - rz * B),
                    cx - fx * L - rx * B, cy - (-fz * L - rz * B), cx - fx * L + rx * B, cy - (-fz * L + rz * B) };
    fb_poly(fb, xy, 4, C(CI_GREEN), 1, 0.5f);
  }
#undef TP
  fb_clip_circle_off(fb);
  fb_ring(fb, cx, cy, R, C(0x2a4a5e), 1);
  fb_text(fb, "N", cx, cy - R + 6 * k, ks, C(CI_CYAN), ALIGN_CENTER, 0);

  /* right column of cards */
  float bx = px + side + 4 * k, bw = vx + vw - 4 * k - bx, y = top;
  float hull = fmaxf(0, d->hp / d->hpmax);
  card(fb, bx, y, bw, 36 * k, "SHIP", ks);
  snprintf(buf, sizeof buf, "HULL %d%%", (int)roundf(hull * 100));
  fb_text(fb, buf, bx + 6 * k, y + 19 * k, ks, C(hull < 0.35f ? CI_RED : CI_TEXT), ALIGN_LEFT, 0);
  meter(fb, bx + 6 * k, y + 25 * k, bw - 12 * k, 4 * k, hull, hull < 0.35f ? CI_RED : CI_GREEN);
  snprintf(buf, sizeof buf, "BOGEYS %d", bogeys);
  fb_text(fb, buf, bx + bw - 6 * k, y + 19 * k, ks, C(bogeys ? CI_RED : CI_DIM), ALIGN_RIGHT, 0);
  y += 40 * k;

  static const char *const MODE[] = { "AUTO", "HOLD", "ORDER", "MANUAL" };
  static const uint32_t MCOL[] = { CI_GREEN, CI_RED, 0xffb84a, CI_CYAN };
  float rowh = 22 * k, th = 13 * k + d->nturrets * rowh;
  card(fb, bx, y, bw, th, "WEAPONS", ks);
  float ry = y + 13 * k;
  for (int t = 0; t < d->nturrets; t++, ry += rowh) {
    const Turret *tu = &d->tur[t];
    bool on = t == d->sel;
    if (on) {
      fb_rect(fb, (int)(bx + 2 * k), (int)ry, (int)(bw - 4 * k), (int)(rowh - 2 * k), C(0x15283a));
      fb_rect_stroke(fb, bx + 2 * k, ry, bw - 4 * k, rowh - 2 * k, C(CI_CYAN), 1);
    }
    fb_text(fb, tu->name, bx + 6 * k, ry + 6 * k, ks, C(on ? 0xffffff : CI_TEXT), ALIGN_LEFT, 0);
    chip(fb, MODE[tu->mode], bx + bw - 6 * k, ry + 6 * k, MCOL[tu->mode], on, ks, ALIGN_RIGHT);
    char tg[24] = "NO TARGET";
    if (ref_get(g, tu->target, NULL, NULL)) contact_label(g, tu->target, tg, sizeof tg);
    fb_text(fb, tg, bx + 6 * k, ry + 15 * k, ks, C(tg[0] == 'N' ? CI_DIM : CI_RED), ALIGN_LEFT, 0);
    float mw = bw * 0.32f, mx = bx + bw - 6 * k - mw;
    if (tu->kind == TUR_CIWS) meter(fb, mx, ry + 13 * k, mw, 3 * k, tu->heat, tu->overheat ? CI_RED : 0xffb84a);
    else {
      for (int i = 0; i < 8; i++)
        fb_rect(fb, (int)(mx + i * mw / 8), (int)(ry + 12 * k), (int)fmaxf(1, mw / 8 - k), (int)(5 * k),
                C(i < tu->ammo ? (tu->reload > 0 ? 0x7a6a30 : CI_TEXT) : 0x1a2634));
    }
  }
  y += th + 4 * k;

  card(fb, bx, y, bw, 26 * k, "CONTACT", ks);
  char pk[24] = "-";
  if (ref_get(g, d->pick, &pp, NULL)) {
    contact_label(g, d->pick, pk, sizeof pk);
    float brg = fmodf(atan2f(pp.x - d->pos.x, pp.z - d->pos.z) * 180 / PI_F + 360, 360);
    snprintf(buf, sizeof buf, "%s  %.1fKM  %03d", pk, v3_dist(pp, d->pos) / 1000, (int)brg);
    fb_text(fb, buf, bx + 6 * k, y + 19 * k, ks, C(0xffffff), ALIGN_LEFT, 0);
  } else fb_text(fb, "NONE PICKED", bx + 6 * k, y + 19 * k, ks, C(CI_DIM), ALIGN_LEFT, 0);
  y += 30 * k;

  static const char *const KEY[][2] = { { "UP/DN", "TURRET" }, { "LT/RT", "CONTACT" }, { "ENTER", "ORDER" },
                                         { "'", "AUTO/HOLD" }, { "P", "LAUNCH" }, { "RSHIFT", "BACK" } };
  float kx = bx, ky = y + 6 * k;
  for (int i = 0; i < 6 && ky < vy + vh - 6 * k; i++) {
    float w = (float)text_width(KEY[i][0], ks) + (float)text_width(KEY[i][1], ks) + 16 * k;
    if (kx + w > bx + bw) {
      kx = bx;
      ky += 12 * k;
      if (ky >= vy + vh - 6 * k) break;
    }
    kx += chip(fb, KEY[i][0], kx, ky, CI_DIM, false, ks, ALIGN_LEFT) + 2 * k;
    fb_text(fb, KEY[i][1], kx, ky, ks, C(CI_DIM), ALIGN_LEFT, 0);
    kx += (float)text_width(KEY[i][1], ks) + 6 * k;
  }
}

/* ---- a manned turret ----------------------------------------------------- */
static void turret_view(Framebuffer *fb, Game *g, int vx, int vy, int vw, int vh) {
  Defense *d = &g->def;
  Turret *t = &d->tur[d->sel];
  vec3 f = v3(sinf(t->yaw) * cosf(t->pitch), sinf(t->pitch), cosf(t->yaw) * cosf(t->pitch));
  /* the sight sits just past the muzzle, so the gun itself stays out of view */
  vec3 at = v3_add(v3_add(defend_turret_pos(g, d->sel), v3(0, 3, 0)), v3_scale(f, 4.5f));
  vec3 r = v3_norm(v3_cross(v3(0, 1, 0), f));
  Camera cam = { at, r, v3_cross(f, r), f, true };
  View V;
  render_scene(fb, g, &cam, vx, vy, vw, vh, &V, NULL);
  fb_clip(fb, vx, vy, vw, vh);
  int ks = (int)fmaxf(1, floorf(fminf(vw / 300.0f, vh / 150.0f)));
  float k = (float)ks, cx = V.cx, cy = V.cy;
  uint32_t hc = C(HUDC);
  fb_ring(fb, cx, cy, 14 * k, hc, k);
  fb_line(fb, cx - 24 * k, cy, cx - 8 * k, cy, hc, k, 1);
  fb_line(fb, cx + 8 * k, cy, cx + 24 * k, cy, hc, k, 1);
  fb_line(fb, cx, cy + 8 * k, cx, cy + 20 * k, hc, k, 1);
  vec3 tp;
  if (t->kind == TUR_SAM && ref_get(g, t->target, &tp, NULL)) {
    vec3 c = to_cam(&cam, tp);
    if (c.z > 1) {
      float sx = cx + c.x / c.z * V.F, sy = cy - c.y / c.z * V.F;
      fb_rect_stroke(fb, sx - 8 * k, sy - 8 * k, 16 * k, 16 * k, C(WARN), k);
      fb_text(fb, "LOCK", sx, sy - 12 * k, ks, C(WARN), ALIGN_CENTER, 1);
    }
  }
  char buf[48];
  fb_text(fb, t->name, vx + 6 * k, vy + 8 * k, ks, hc, ALIGN_LEFT, 1);
  if (t->kind == TUR_CIWS) {
    fb_text(fb, t->overheat ? "OVERHEAT" : "HEAT", vx + 6 * k, vy + 20 * k, ks, C(t->overheat ? WARN : DIM), ALIGN_LEFT, 1);
    fb_rect(fb, (int)(vx + 50 * k), (int)(vy + 17 * k), (int)(60 * k), (int)(6 * k), C(0x2a2350));
    fb_rect(fb, (int)(vx + 50 * k), (int)(vy + 17 * k), (int)(60 * k * fminf(1, t->heat)), (int)(6 * k), C(t->overheat ? WARN : AMBER));
  } else {
    snprintf(buf, sizeof buf, "MISSILES %d%s", t->ammo, t->reload > 0 ? "  RELOADING" : "");
    fb_text(fb, buf, vx + 6 * k, vy + 20 * k, ks, C(INK), ALIGN_LEFT, 1);
  }
  snprintf(buf, sizeof buf, "HULL %d%%", (int)fmaxf(0, d->hp / d->hpmax * 100));
  fb_text(fb, buf, vx + vw - 6 * k, vy + 8 * k, ks, C(d->hp < d->hpmax * 0.35f ? WARN : INK), ALIGN_RIGHT, 1);
  fb_text(fb, "ARROWS AIM  ENTER FIRE  RSHIFT STEP BACK", cx, vy + vh - 8 * k, ks, C(DIM), ALIGN_CENTER, 1);
  fb_clip_reset(fb);
}

/* ---- on foot ------------------------------------------------------------- */
static void walk_view(Framebuffer *fb, Game *g, int vx, int vy, int vw, int vh) {
  Defense *d = &g->def;
  float yaw = d->heading * PI_F / 180 + d->cap_yaw, lp = d->cap_look;
  vec3 f = v3(sinf(yaw) * cosf(lp), sinf(lp), cosf(yaw) * cosf(lp));
  vec3 r = v3_norm(v3_cross(v3(0, 1, 0), f));
  Camera cam = { defend_cap_eye(g), r, v3_cross(f, r), f, true };
  View V;
  r3d_inside_bridge(d->cap_bridge);
  r3d_hide_captain(true);
  r3d_near(0.15f);
  render_scene(fb, g, &cam, vx, vy, vw, vh, &V, NULL);
  r3d_near(NEAR_Z);
  r3d_inside_bridge(false);
  r3d_hide_captain(false);
  fb_clip(fb, vx, vy, vw, vh);
  int ks = (int)fmaxf(1, floorf(fminf(vw / 300.0f, vh / 150.0f)));
  float k = (float)ks;
  char buf[64];
  fb_rect(fb, (int)(V.cx - k / 2), (int)(V.cy - k / 2), ks, ks, C(0xffffff));
  fb_text(fb, d->cap_bridge ? "BRIDGE" : "FLIGHT DECK", vx + 6 * k, vy + 8 * k, ks, C(0x7af0ff), ALIGN_LEFT, 1);
  snprintf(buf, sizeof buf, "HULL %d%%", (int)fmaxf(0, d->hp / d->hpmax * 100));
  fb_text(fb, buf, vx + vw - 6 * k, vy + 8 * k, ks, C(d->hp < d->hpmax * 0.35f ? WARN : INK), ALIGN_RIGHT, 1);
  if (d->between) snprintf(buf, sizeof buf, "WAVE %d IN %d", d->wave + 1, (int)ceilf(d->waveT));
  else snprintf(buf, sizeof buf, "WAVE %d/%d", d->wave, d->waves);
  fb_text(fb, buf, vx + vw - 6 * k, vy + 18 * k, ks, C(INK), ALIGN_RIGHT, 1);
  bool vamp = false;
  for (int i = 0; i < g->nmissiles; i++) vamp |= g->missiles[i].ashm;
  if (vamp && ((int)(g->time * 4)) % 2) fb_text(fb, "VAMPIRE INBOUND", V.cx, vy + 22 * k, ks, C(WARN), ALIGN_CENTER, 1);
  Usable u = defend_usable(g);
  if (u.kind != USE_NONE) {
    snprintf(buf, sizeof buf, "ENTER  %s", u.label);
    fb_text(fb, buf, V.cx, V.cy + 26 * k, ks, C(0xffffff), ALIGN_CENTER, 1);
  }
  fb_text(fb, "ARROWS WALK  P/; LOOK  ' RUN  ENTER USE", V.cx, vy + vh - 8 * k, ks, C(DIM), ALIGN_CENTER, 1);
  fb_clip_reset(fb);
}

void command_render(Framebuffer *fb, Game *g, int vx, int vy, int vw, int vh) {
  fb_clip(fb, vx, vy, vw, vh);
  if (g->def.cmd_state == CMD_WALK) walk_view(fb, g, vx, vy, vw, vh);
  else if (g->def.cmd_state == CMD_TURRET) turret_view(fb, g, vx, vy, vw, vh);
  else room(fb, g, vx, vy, vw, vh);
  fb_clip_reset(fb);
}
