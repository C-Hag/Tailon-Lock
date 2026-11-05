/* test_figure.c - the figure engine against Leonardo's canon: every ratio of the Vitruvian Man measured on the built
 * mesh (not on the skeleton's numbers) within 2 %, and the hands and feet of the spread pose on one circle round the
 * navel. With --dump it prints the triangles of a crew member, for the comparison with the browser game's engine
 * (assets/figure/parity.js); with --portrait it draws a crew member's head as a PNG. Original code for this project. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "figure.h"
#include "room.h"
#include "core/safe.h"

static int fails;
/* the film's spoken lines, as their dialogue cards show them (assets/intro/intro_script.json) */
static const struct { const char *who, *text; float t0, t1; int words; } LINES[3] = {
  { "RADAR", "BOGEYS! TWO CONTACTS, BEARING 3-5-0, CLOSING FAST.", 5.6f, 7.0f, 9 },
  { "SUPERVISOR", "ALERT, ALERT! WE HAVE INBOUND.", 7.05f, 8.05f, 5 },
  { "COMMANDER", "SEND THE BIRDS.", 8.1f, 9.2f, 3 } };
/* ---- --portrait: a crew member's head and shoulders from the front, three-quarters and the side, at rest looking
 * at us and then speaking with his eyes turned away, as a PNG (stored, uncompressed: our own writer) ---- */
static uint32_t crc_tab[256];
static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t c) {
  if (!crc_tab[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t k = i;
      for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1;
      crc_tab[i] = k;
    }
  c = ~c;
  for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 255] ^ (c >> 8);
  return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, size_t n) {
  uint8_t h[8];
  be32(h, (uint32_t)n), memcpy(h + 4, type, 4);
  fwrite(h, 1, 8, f), fwrite(data, 1, n, f);
  uint32_t c = crc32_of((const uint8_t *)type, 4, 0);
  c = crc32_of(data, n, c);
  be32(h, c), fwrite(h, 1, 4, f);
}
static int write_png(const char *path, const uint32_t *px, int W, int H) {
  size_t row = (size_t)W * 3 + 1, raw_n = row * H, blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4;
  uint8_t *raw = malloc(raw_n), *z = malloc(z_n);
  if (!raw || !z) {
    free(raw), free(z);
    return 1;
  }
  for (int y = 0; y < H; y++) {
    raw[y * row] = 0;
    for (int x = 0; x < W; x++) {
      uint32_t v = px[(size_t)y * W + x];
      raw[y * row + 1 + x * 3] = (uint8_t)(v >> 16), raw[y * row + 2 + x * 3] = (uint8_t)(v >> 8), raw[y * row + 3 + x * 3] = (uint8_t)v;
    }
  }
  size_t at = 0, left = raw_n, from = 0;
  uint32_t a = 1, b = 0;
  z[at++] = 0x78, z[at++] = 0x01;
  while (left) {
    size_t n = left > 65535 ? 65535 : left;
    z[at++] = left == n, z[at++] = (uint8_t)n, z[at++] = (uint8_t)(n >> 8), z[at++] = (uint8_t)~n, z[at++] = (uint8_t)(~n >> 8);
    memcpy(z + at, raw + from, n);
    for (size_t i = 0; i < n; i++) a = (a + raw[from + i]) % 65521, b = (b + a) % 65521;
    at += n, from += n, left -= n;
  }
  be32(z + at, b << 16 | a), at += 4;
  FILE *f = fopen(path, "wb");
  if (!f) {
    free(raw), free(z);
    return 1;
  }
  static const uint8_t SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  uint8_t ih[13];
  be32(ih, (uint32_t)W), be32(ih + 4, (uint32_t)H), ih[8] = 8, ih[9] = 2, ih[10] = ih[11] = ih[12] = 0;
  fwrite(SIG, 1, 8, f);
  chunk(f, "IHDR", ih, 13), chunk(f, "IDAT", z, at), chunk(f, "IEND", NULL, 0);
  fclose(f);
  free(raw), free(z);
  return 0;
}
static int portrait(int who, const char *path) {
  enum { TW = 420, TH = 520, COLS = 4, ROWS = 2 };
  static uint32_t px[TW * COLS * TH * ROWS];
  static FigTri t[FIG_MAX_TRIS];
  static const float YAW[COLS] = { 0, 30, 60, 90 };
  for (size_t i = 0; i < sizeof px / sizeof px[0]; i++) px[i] = 0xFF2A2F36u;
  static uint32_t tile[TW * TH];
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      FigSpec f;
      fig_crew(who, 1.75f, 0, &f);
      fig_pose_stand(&f.pose);
      f.pose.yaw = YAW[c];
      f.detail = 1;
      float hd[3];
      fig_joint(&f, FB_HEAD, hd);
      FigCam cam = { { hd[0], hd[1] + 0.02f, hd[2] + 1.3f }, { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 }, 1600, TW / 2.0f, TH * 0.42f };
      if (r == 0) memcpy(f.face.gaze, cam.pos, sizeof f.face.gaze); /* at us */
      else { /* speaking, looking up and to his right */
        f.face.open = 0.6f, f.face.wide = 0.3f;
        float g[3] = { hd[0] - 0.8f, hd[1] + 0.5f, hd[2] + 1.0f };
        memcpy(f.face.gaze, g, sizeof g);
      }
      f.face.has_gaze = true;
      int n = fig_build(&f, t, FIG_MAX_TRIS);
      if (n < 0) return 1;
      for (int i = 0; i < TW * TH; i++) tile[i] = 0xFF2A2F36u;
      FigMesh m = { t, n, &f };
      FigLight L = { { -0.45f, 0.5f, 0.74f }, { 235, 225, 210 }, { 90, 150, 230 } };
      fig_raster(&m, 1, &cam, &L, 1, 0, 0, tile, TW, TH);
      for (int y = 0; y < TH; y++) memcpy(&px[(size_t)(r * TH + y) * TW * COLS + c * TW], &tile[(size_t)y * TW], TW * sizeof tile[0]);
    }
  return write_png(path, px, TW * COLS, TH * ROWS);
}
static void ratio(const char *name, double got, double want) {
  double off = (got / want - 1) * 100;
  int bad = fabs(off) > 2;
  printf("  %-22s canon %.4f  mesh %.4f  %+5.1f %%%s\n", name, want, got, off, bad ? "  OUT" : "");
  fails += bad;
}

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "--dump")) {
    /* the triangles of one crew member: who (0-2), raise */
    static FigTri t[FIG_MAX_TRIS];
    FigSpec f;
    fig_crew(argv[2][0] - '0', 1.75f, argc > 3 ? (float)safe_num(argv[3], 0) : 0, &f);
    if (argc > 4) f.face.open = (float)safe_num(argv[4], 0), f.face.wide = 0.3f, f.face.smile = 0.2f, f.face.brow_up = 0.5f, f.face.blink = 0.3f;
    if (argc > 5) f.detail = (int)safe_int(argv[5], 0, INT_MIN, INT_MAX); /* the close-up head */
    int n = fig_build(&f, t, FIG_MAX_TRIS);
    printf("%d\n", n);
    for (int i = 0; i < n; i++) {
      printf("%d %d %d %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %d", t[i].role, t[i].part, t[i].two, t[i].v[0][0], t[i].v[0][1], t[i].v[0][2],
             t[i].v[1][0], t[i].v[1][1], t[i].v[1][2], t[i].v[2][0], t[i].v[2][1], t[i].v[2][2], t[i].shade);
      if (t[i].shade == FS_SMOOTH)
        for (int q = 0; q < 3; q++) printf(" %.4f %.4f %.4f", t[i].cs[q][0], t[i].cs[q][1], t[i].cs[q][2]);
      else if (t[i].shade == FS_OWN || t[i].shade == FS_LIT) printf(" %.4f %.4f %.4f", t[i].col[0], t[i].col[1], t[i].col[2]);
      printf("\n");
    }
    return n > 0 ? 0 : 1;
  }
  if (argc > 3 && !strcmp(argv[1], "--portrait")) return portrait(argv[2][0] - '0', argv[3]); /* who (0-2), the PNG */
  if (argc > 1 && !strcmp(argv[1], "--room")) { /* the room, its screens, the two chairs, and the room lit at general quarters */
    static FigTri t[ROOM_MAX_TRIS + 2 * ROOM_CHAIR_TRIS], lit[ROOM_MAX_TRIS + 2 * ROOM_CHAIR_TRIS];
    static RoomScreen scr[ROOM_MAX_SCREENS];
    int ns = 0, n = room_build(t, ROOM_MAX_TRIS, scr, ROOM_MAX_SCREENS, &ns);
    if (n < 0) return 1;
    static const float S1[3] = { -0.1f, 0.5f, 1.6f }, S2[3] = { 1.3f, 0.62f, 1.4f };
    int c1 = room_chair(false, S1, -108, t + n, ROOM_CHAIR_TRIS);
    int c2 = room_chair(true, S2, -100, t + n + c1, ROOM_CHAIR_TRIS);
    RoomLights L;
    room_lamps(scr, ns, true, 0.5f, &L);
    room_light(t, lit, n + c1 + c2, &L);
    printf("%d %d %d %d\n", n, c1, c2, ns);
    for (int i = 0; i < ns; i++)
      printf("%d %d %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %g %g\n", scr[i].kind, scr[i].bay, scr[i].o[0], scr[i].o[1], scr[i].o[2], scr[i].ex[0],
             scr[i].ex[1], scr[i].ex[2], scr[i].ey[0], scr[i].ey[1], scr[i].ey[2], scr[i].w, scr[i].h);
    for (int i = 0; i < n + c1 + c2; i++)
      printf("%d %d %d %d %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.3f %.3f %.3f %.3f %.3f %.3f\n", t[i].part, t[i].two, t[i].emit, t[i].tag, t[i].v[0][0],
             t[i].v[0][1], t[i].v[0][2], t[i].v[1][0], t[i].v[1][1], t[i].v[1][2], t[i].v[2][0], t[i].v[2][1], t[i].v[2][2], t[i].col[0], t[i].col[1], t[i].col[2],
             lit[i].col[0], lit[i].col[1], lit[i].col[2]);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "--speech")) { /* the three lines' timelines */
    for (int l = 0; l < 3; l++) {
      FigSpeech sp;
      fig_speech(LINES[l].text, LINES[l].t0, LINES[l].t1, &sp);
      printf("%d %d\n", sp.nseg, sp.nwords);
      for (int i = 0; i < sp.nseg; i++) printf("%.5f %.5f %d\n", sp.t0[i], sp.t1[i], sp.vis[i]);
      for (int i = 0; i < sp.nwords; i++) printf("%.5f %s\n", sp.start[i], sp.word[i]);
    }
    return 0;
  }
  FigRatios m, c;
  fig_canon(&c);
  for (double H = 1.6; H < 1.95; H += 0.15) {
    if (!fig_measure(H, &m)) {
      printf("test_figure: no mesh at %.2f m\n", H);
      return 1;
    }
    printf("figure %.2f m, measured height %.3f m\n", H, m.height);
    ratio("arm span", m.span, c.span);
    ratio("head", m.head, c.head);
    ratio("face", m.face, c.face);
    ratio("face, lower third", m.thirds[0], c.thirds[0]);
    ratio("face, middle third", m.thirds[1], c.thirds[1]);
    ratio("face, upper third", m.thirds[2], c.thirds[2]);
    ratio("crown to chest", m.chest_top, c.chest_top);
    ratio("widest shoulders", m.shoulders, c.shoulders);
    ratio("elbow to fingertip", m.elbow_tip, c.elbow_tip);
    ratio("elbow to armpit", m.elbow_pit, c.elbow_pit);
    ratio("hand", m.hand, c.hand);
    ratio("foot", m.foot, c.foot);
    ratio("sole to knee", m.knee, c.knee);
    ratio("knee to crotch", m.knee_crotch, c.knee_crotch);
    ratio("navel's circle", m.circle, c.circle);
    ratio("height", m.height / H, 1);
  }
  /* the face's rules, measured on each crew member's mesh */
  static const char *const WHO[3] = { "radar operator", "supervisor", "commander" };
  for (int w = 0; w < 3; w++) {
    FigFaceRules r;
    if (!fig_face_rules(w, &r)) {
      printf("test_figure: no face for the %s\n", WHO[w]);
      return 1;
    }
    printf("face of the %s\n", WHO[w]);
    ratio("eyes at half the head", r.eye_height, 1);
    ratio("gap = one eye", r.eye_gap, 1);
    ratio("nose = the gap", r.nose_width, 1);
    ratio("mouth under pupils", r.mouth_corners, 1);
    ratio("ear's top at the brow", r.ear_top, 1);
    ratio("ear's foot at the nose", r.ear_bottom, 1);
    ratio("globe 24 mm", r.globe, 1);
    ratio("eyes 63 mm apart", r.ipd, 1);
    ratio("opening 31.5 mm wide", r.open_w, 1);
    ratio("opening 10.2 mm tall", r.open_h, 1);
    ratio("iris 11.7 mm", r.iris, 1);
    ratio("pupil 5.5 mm", r.pupil, 1);
    ratio("cornea behind the face", r.cornea_behind, 1);
  }
  /* each line's mouth shapes run from its card's start to its end, with no gaps, every word spoken */
  for (int l = 0; l < 3; l++) {
    FigSpeech sp;
    fig_speech(LINES[l].text, LINES[l].t0, LINES[l].t1, &sp);
    double gap = 0;
    for (int i = 1; i < sp.nseg; i++) gap = fmax(gap, fabs(sp.t0[i] - sp.t1[i - 1]));
    int bad = fabs(sp.t0[0] - LINES[l].t0) > 1e-4 || fabs(sp.t1[sp.nseg - 1] - LINES[l].t1) > 1e-4 || gap > 1e-4 || sp.nwords != LINES[l].words;
    for (int i = 0; i < sp.nwords; i++) bad |= sp.start[i] < LINES[l].t0 || sp.start[i] > LINES[l].t1;
    printf("  %-10s %2d mouth shapes over %.2f-%.2f s, %d words, largest gap %.1e s%s\n", LINES[l].who, sp.nseg, LINES[l].t0, LINES[l].t1, sp.nwords,
           gap, bad ? "  OUT" : "");
    fails += bad;
  }
  /* the camera turned away from the figure: nothing is drawn, and the empty box is never cast to int */
  {
    static FigTri t[FIG_MAX_TRIS];
    static uint32_t px[64 * 64];
    FigSpec f;
    fig_crew(0, 1.75f, 0, &f);
    fig_pose_stand(&f.pose);
    int n = fig_build(&f, t, FIG_MAX_TRIS);
    FigCam cam = { { 0, 1.6f, 3 }, { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 }, 800, 32, 32 };
    FigLight L = { { -0.45f, 0.5f, 0.74f }, { 235, 225, 210 }, { 90, 150, 230 } };
    for (int i = 0; i < 64 * 64; i++) px[i] = 0xFF123456u;
    FigMesh fm = { t, n, &f };
    if (n > 0) fig_raster(&fm, 1, &cam, &L, 1, 0, 0, px, 64, 64);
    int drawn = 0;
    for (int i = 0; i < 64 * 64; i++) drawn += px[i] != 0xFF123456u;
    printf("  %-22s %d triangles, %d pixels drawn%s\n", "camera turned away", n, drawn, n <= 0 || drawn ? "  OUT" : "");
    fails += n <= 0 || drawn;
  }
  printf("test_figure: %s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
