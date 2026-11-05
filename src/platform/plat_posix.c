/* plat_posix.c - Linux, macOS (and other POSIX) side of plat.h. */
#define _POSIX_C_SOURCE 200809L /* readlink, realpath */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE /* the system's own calls besides POSIX */
#endif
#include "platform/plat.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include "brand.h"

#ifndef __APPLE__ /* only the Linux folder had an old name */
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
    bool ok = in && out;
    for (size_t k; ok && (k = fread(chunk, 1, sizeof chunk, in)) > 0;) {
      ok = fwrite(chunk, 1, k, out) == k;
      if (k < sizeof chunk) break; /* the end, or an error: no reading past it */
    }
    if (in && ferror(in)) ok = false;
    if (out && fclose(out) != 0) ok = false;
    if (in) fclose(in);
    if (out && !ok) remove(b); /* no half copy: the old folder still has the file */
  }
  closedir(d);
}
#endif

int plat_config_dir(char *buf, size_t size) {
  /* tests: A4C_CONFIG_DIR is the settings folder itself, so a test run never reads or writes the player's own */
  const char *test_dir = getenv("A4C_CONFIG_DIR");
  if (test_dir && *test_dir) {
    int t = snprintf(buf, size, "%s", test_dir);
    return t > 0 && (size_t)t < size;
  }
#ifdef __APPLE__
  /* macOS keeps an app's settings in ~/Library/Application Support/<app> */
  const char *home = getenv("HOME");
  if (!home || !*home) return 0;
  int n = snprintf(buf, size, "%s/Library/Application Support/" GAME_DIR_WIN, home);
  return n > 0 && (size_t)n < size;
#else
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
#endif
}

int plat_random(void *buf, size_t size) {
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f) return 0;
  size_t got = fread(buf, 1, size, f);
  fclose(f);
  return got == size;
}

#ifdef __APPLE__
/* [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated reason:@"..."] through the
 * Objective-C runtime's C calls, kept for the life of the process (no Objective-C file needed) */
void plat_busy(void) {
  static id activity;
  if (activity) return;
  Class pi = objc_getClass("NSProcessInfo"), str = objc_getClass("NSString");
  if (!pi || !str) return;
  id info = ((id(*)(Class, SEL))objc_msgSend)(pi, sel_registerName("processInfo"));
  id why = ((id(*)(Class, SEL, const char *))objc_msgSend)(str, sel_registerName("stringWithUTF8String:"), "unattended test run");
  activity = ((id(*)(id, SEL, unsigned long long, id))objc_msgSend)(info, sel_registerName("beginActivityWithOptions:reason:"),
                                                                     0x00FFFFFFULL /* NSActivityUserInitiated */, why);
  if (activity) ((id(*)(id, SEL))objc_msgSend)(activity, sel_registerName("retain"));
}
#else
void plat_busy(void) {}
#endif

int  plat_net_init(void) { return 1; }
void plat_net_quit(void) {}

const char *plat_base_path(void) {
  static char base[1024];
  if (base[0]) return base;
#ifdef __APPLE__
  char raw[1024];
  uint32_t len = sizeof raw;
  if (_NSGetExecutablePath(raw, &len) != 0 || !realpath(raw, base)) return base[0] = 0, NULL;
#else
  ssize_t n = readlink("/proc/self/exe", base, sizeof base - 1);
  if (n <= 0) return base[0] = 0, NULL;
  base[n] = 0;
#endif
  char *slash = strrchr(base, '/');
  if (!slash) return base[0] = 0, NULL;
  slash[1] = 0;
#ifdef __APPLE__
  /* inside an app bundle (TailonLock.app/Contents/MacOS/) the files are in Contents/Resources/ */
  size_t bl = strlen(base);
  static const char macos[] = "/Contents/MacOS/";
  if (bl >= sizeof macos - 1 && strcmp(base + bl - (sizeof macos - 1), macos) == 0)
    snprintf(base + bl - (sizeof macos - 1), sizeof base - bl + (sizeof macos - 1), "/Contents/Resources/");
#endif
  return base;
}

bool plat_is_file(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool plat_make_dir(const char *path) {
  char p[1024];
  int n = snprintf(p, sizeof p, "%s", path);
  if (n <= 0 || (size_t)n >= sizeof p) return false;
  for (char *c = p + 1; *c; c++) /* the folders above it first */
    if (*c == '/') {
      *c = 0;
      mkdir(p, 0755);
      *c = '/';
    }
  struct stat st;
  return mkdir(p, 0755) == 0 || (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
}

bool plat_file_sync(FILE *f) {
  if (fflush(f) != 0) return false;
#ifdef __APPLE__
  if (fcntl(fileno(f), F_FULLFSYNC) == 0) return true; /* fsync alone leaves it in the drive's cache on macOS */
#endif
  return fsync(fileno(f)) == 0;
}

bool plat_file_replace(const char *from, const char *to) {
  if (rename(from, to) != 0) return false; /* POSIX: rename replaces to in one step */
  /* and the folder's new entry to the disk too (best effort: the file is in place either way) */
  char dir[1024];
  const char *slash = strrchr(to, '/');
  int n = slash ? snprintf(dir, sizeof dir, "%.*s", (int)(slash - to) + (slash == to), to) : snprintf(dir, sizeof dir, ".");
  if (n > 0 && (size_t)n < sizeof dir) {
    int fd = open(dir, O_RDONLY);
    if (fd >= 0) {
      fsync(fd);
      close(fd);
    }
  }
  return true;
}
