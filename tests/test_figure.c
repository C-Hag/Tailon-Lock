/* test_figure.c - the figure engine against Leonardo's canon: every ratio of the Vitruvian Man measured on the built
 * mesh (not on the skeleton's numbers) within 2 %, and the hands and feet of the spread pose on one circle round the
 * navel. With --dump it prints the triangles of a crew member, for the comparison with the browser game's engine
 * (assets/figure/parity.js). Original code for this project. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "figure.h"

static int fails;
/* the film's spoken lines, as their dialogue cards show them (assets/intro/intro_script.json) */
static const struct { const char *who, *text; float t0, t1; int words; } LINES[3] = {
  { "RADAR", "BOGEYS! TWO CONTACTS, BEARING 3-5-0, CLOSING FAST.", 5.6f, 7.0f, 9 },
  { "SUPERVISOR", "ALERT, ALERT! WE HAVE INBOUND.", 7.05f, 8.05f, 5 },
  { "COMMANDER", "SEND THE BIRDS.", 8.1f, 9.2f, 3 } };
static void ratio(const char *name, double got, double want) {
  double off = (got / want - 1) * 100;
  int bad = fabs(off) > 2;
  printf("  %-22s canon %.4f  mesh %.4f  %+5.1f %%%s\n", name, want, got, off, bad ? "  OUT" : "");
  fails += bad;
}

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "--dump")) {
    /* the triangles of one crew member: who (0-2), raise */
    static FigTri t[8192];
    FigSpec f;
    fig_crew(argv[2][0] - '0', 1.75f, argc > 3 ? (float)atof(argv[3]) : 0, &f);
    if (argc > 4) f.face.open = (float)atof(argv[4]), f.face.wide = 0.3f, f.face.smile = 0.2f, f.face.brow_up = 0.5f, f.face.blink = 0.3f;
    int n = fig_build(&f, t, 8192);
    printf("%d\n", n);
    for (int i = 0; i < n; i++) {
      printf("%d %d %d %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %d", t[i].role, t[i].part, t[i].two, t[i].v[0][0], t[i].v[0][1], t[i].v[0][2],
             t[i].v[1][0], t[i].v[1][1], t[i].v[1][2], t[i].v[2][0], t[i].v[2][1], t[i].v[2][2], t[i].shade);
      if (t[i].shade == FS_SMOOTH)
        for (int q = 0; q < 3; q++) printf(" %.4f %.4f %.4f", t[i].cs[q][0], t[i].cs[q][1], t[i].cs[q][2]);
      else if (t[i].shade == FS_OWN) printf(" %.4f %.4f %.4f", t[i].col[0], t[i].col[1], t[i].col[2]);
      printf("\n");
    }
    return n > 0 ? 0 : 1;
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
  printf("test_figure: %s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
