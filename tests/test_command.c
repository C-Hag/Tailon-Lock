/* test_command.c - the carrier commander's contact pick, on command.c's own function: a picked missile stays
 * picked when the missile list closes up under it (its slot changes, its id does not). Original code for this
 * project. */
#include "../src/command.c"

/* the drawing command.c asks of the renderer (none here), and the save calls plat_common.c names */
void r3d_hide_captain(bool on) { (void)on; }
void r3d_inside_bridge(bool on) { (void)on; }
void r3d_near(float z) { (void)z; }
void render_scene(Framebuffer *fb, Game *g, const Camera *cam, int vx, int vy, int vw, int sh, View *out, const Plane *hide) {
  (void)fb, (void)g, (void)cam, (void)vx, (void)vy, (void)vw, (void)sh, (void)hide;
  memset(out, 0, sizeof *out); /* the real one fills the view; the callers read it */
}
bool plat_file_sync(FILE *f) { return (void)f, false; }
bool plat_file_replace(const char *from, const char *to) { return (void)from, (void)to, false; }
void plat_log(const char *fmt, ...) { (void)fmt; } /* plat_common.c's TL_FILES line (tools/ff/inventory) */

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_command:%d: %s\n", __LINE__, #c);          \
      fails++;                                                         \
    }                                                                  \
  } while (0)

int main(void) {
  static Game g;
  game_new(&g, MODE_DEFEND, DIFF_NORMAL, true, NULL, 5u);
  g.def.cmd_state = CMD_TACTICAL;
  for (int i = 0; i < g.nplanes; i++) g.planes[i].alive = false; /* missiles only */
  /* three anti-ship missiles inbound, the nearest first */
  g.nmissiles = 3;
  for (int i = 0; i < 3; i++) {
    memset(&g.missiles[i], 0, sizeof g.missiles[i]);
    g.missiles[i].id = 100u + (uint32_t)i;
    g.missiles[i].ashm = true;
    g.missiles[i].team = 1;
    g.missiles[i].pos = v3(g.def.pos.x + 3000.0f * (float)(i + 1), 50, g.def.pos.z);
  }
  /* pick the middle one (id 101, slot 1), then the nearest goes down and the list closes up: 101 is in slot 0 */
  Ref r = { REF_MISSILE, 1, 101 };
  g.def.pick = r;
  g.missiles[0] = g.missiles[1];
  g.missiles[1] = g.missiles[2];
  g.nmissiles = 2;
  command_action(&g, CA_RIGHT); /* the next one out from the pick: 102 */
  CHECK(g.def.pick.kind == REF_MISSILE && g.def.pick.gen == 102);
  if (fails) fprintf(stderr, "test_command: %d failed\n", fails);
  else printf("test_command: ok\n");
  return fails != 0;
}
