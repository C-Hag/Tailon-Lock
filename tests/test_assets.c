/* test_assets.c - assets.c's readers on data that is short or odd: a number array with fewer numbers than it
 * should have reads as 0s, not through NULL, and a jet sheet's text that grows when it is made plain stays inside
 * its field. Original code for this project. */
#include "../src/assets.c"

/* the little of the platform assets.c calls */
void plat_log(const char *fmt, ...) { (void)fmt; }
const char *plat_base_path(void) { return NULL; }
bool plat_is_file(const char *path) { return (void)path, false; }
bool plat_file_sync(FILE *f) { return (void)f, false; }
bool plat_file_replace(const char *from, const char *to) { return (void)from, (void)to, false; }

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_assets:%d: %s\n", __LINE__, #c);           \
      fails++;                                                         \
    }                                                                  \
  } while (0)

static void test_short_arrays(void) {
  Json *j = json_parse("[[1,2],[],[1,2,3]]", 18, NULL);
  CHECK(j != NULL);
  if (!j) return;
  vec3 a = vec_of(json_at(j, 0)), b = vec_of(json_at(j, 1)), c = vec_of(json_at(j, 2)), d = vec_of(NULL);
  CHECK(a.x == 1 && a.y == 2 && a.z == 0);
  CHECK(b.x == 0 && b.y == 0 && b.z == 0 && d.x == 0);
  CHECK(c.x == 1 && c.y == 2 && c.z == 3);
  json_free(j);
}

static void test_plain(void) {
  struct { char before[8], s[16], after[8]; } f;
  memset(&f, 0x55, sizeof f);
  snprintf(f.s, sizeof f.s, "~~~~~~~~~~~~~~~"); /* 15 tildes, each to grow to "ABOUT " */
  plain(f.s, sizeof f.s);
  CHECK(strlen(f.s) < sizeof f.s && !strncmp(f.s, "ABOUT ABOUT ", 12));
  int clean = 1;
  for (size_t i = 0; i < sizeof f.after; i++) clean &= (unsigned char)f.after[i] == 0x55;
  CHECK(clean); /* nothing written past the field */
  char t[64] = "MACH 2\xc2\xb7" "5 \xe2\x80\x93 9 \xc3\x97 3";
  plain(t, sizeof t);
  CHECK(!strcmp(t, "MACH 2-5 - 9 x 3"));
}

/* a base's heading: north or south, as the runway logic has it */
static void test_runway_heading(void) {
  CHECK(runway_heading(0, "t", 0) == 0 && runway_heading(180, "t", 0) == 180 && runway_heading(-180, "t", 0) == 180);
  CHECK(runway_heading(30, "t", 0) == 0 && runway_heading(100, "t", 0) == 180 && runway_heading(260, "t", 1) == 180);
  CHECK(runway_heading(-60, "t", 1) == 0 && runway_heading(360, "t", 1) == 0);
}

int main(void) {
  test_runway_heading();
  test_short_arrays();
  test_plain();
  if (fails) fprintf(stderr, "test_assets: %d failed\n", fails);
  else printf("test_assets: ok\n");
  return fails != 0;
}
