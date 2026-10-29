/* plat_posix.c - Linux (and other POSIX) side of plat.h. */
#include "platform/plat.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "brand.h"

/* The game was renamed: if the new folder isn't there yet but the old one is, copy the old one's files over once
 * (copied, not moved, so an older build still finds its settings). */
static void migrate(const char *from, const char *to) {
  struct stat st;
  if (stat(to, &st) == 0 || stat(from, &st) != 0 || !S_ISDIR(st.st_mode)) return;
  if (mkdir(to, 0700) != 0) return;
  DIR *d = opendir(from);
  if (!d) return;
  for (struct dirent *e; (e = readdir(d));) {
    char a[2048], b[2048];
    int na = snprintf(a, sizeof a, "%s/%s", from, e->d_name), nb = snprintf(b, sizeof b, "%s/%s", to, e->d_name);
    if (na < 0 || nb < 0 || (size_t)na >= sizeof a || (size_t)nb >= sizeof b) continue;   /* too long: skip it */
    if (stat(a, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    FILE *in = fopen(a, "rb"), *out = in ? fopen(b, "wb") : NULL;
    char chunk[8192];
    for (size_t k; in && out && (k = fread(chunk, 1, sizeof chunk, in)) > 0;) fwrite(chunk, 1, k, out);
    if (out) fclose(out);
    if (in) fclose(in);
  }
  closedir(d);
}

int plat_config_dir(char *buf, size_t size) {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  char base[768];
  int n;
  if (xdg && *xdg) n = snprintf(base, sizeof base, "%s", xdg);
  else if (home && *home) n = snprintf(base, sizeof base, "%s/.config", home);
  else return 0;
  if (n <= 0 || (size_t)n >= sizeof base) return 0;
  char old[1024];
  snprintf(old, sizeof old, "%s/" OLD_ID, base);
  n = snprintf(buf, size, "%s/" GAME_ID, base);
  if (n <= 0 || (size_t)n >= size) return 0;
  migrate(old, buf);
  return 1;
}

int plat_random(void *buf, size_t size) {
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f) return 0;
  size_t got = fread(buf, 1, size, f);
  fclose(f);
  return got == size;
}

int  plat_net_init(void) { return 1; }
void plat_net_quit(void) {}
