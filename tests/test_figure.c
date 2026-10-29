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
static void ratio(const char *name, double got, double want) {
  double off = (got / want - 1) * 100;
  int bad = fabs(off) > 2;
  printf("  %-22s canon %.4f  mesh %.4f  %+5.1f %%%s\n", name, want, got, off, bad ? "  OUT" : "");
  fails += bad;
}

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "--dump")) {
    /* the triangles of one crew member: who (0-2), raise */
    static FigTri t[4096];
    FigSpec f;
    fig_crew(argv[2][0] - '0', 1.75f, argc > 3 ? (float)atof(argv[3]) : 0, &f);
    int n = fig_build(&f, t, 4096);
    printf("%d\n", n);
    for (int i = 0; i < n; i++)
      printf("%d %d %d %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f\n", t[i].role, t[i].part, t[i].two, t[i].v[0][0], t[i].v[0][1], t[i].v[0][2],
             t[i].v[1][0], t[i].v[1][1], t[i].v[1][2], t[i].v[2][0], t[i].v[2][1], t[i].v[2][2]);
    return n > 0 ? 0 : 1;
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
  printf("test_figure: %s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
