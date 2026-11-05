/* plat_win32.c - Windows side of plat.h. Keep to APIs that exist on
 * Windows 10 and older too; nothing Windows 11-only. */
#include "platform/plat.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "brand.h"

/* The game was renamed: if the new folder isn't there yet but the old one is, copy the old one's files over once
 * (copied, not moved, so an older build still finds its settings). */
static void migrate(const char *from, const char *to) {
  if (GetFileAttributesA(to) != INVALID_FILE_ATTRIBUTES) return;
  DWORD fa = GetFileAttributesA(from);
  if (fa == INVALID_FILE_ATTRIBUTES || !(fa & FILE_ATTRIBUTE_DIRECTORY)) return;
  if (!CreateDirectoryA(to, NULL)) return;
  char pat[1024];
  snprintf(pat, sizeof pat, "%s\\*", from);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    char a[1024], b[1024];
    snprintf(a, sizeof a, "%s\\%s", from, fd.cFileName);
    snprintf(b, sizeof b, "%s\\%s", to, fd.cFileName);
    CopyFileA(a, b, TRUE);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

int plat_config_dir(char *buf, size_t size) {
  /* tests: A4C_CONFIG_DIR is the settings folder itself, so a test run never reads or writes the player's own */
  const char *test_dir = getenv("A4C_CONFIG_DIR");
  if (test_dir && *test_dir) {
    int t = snprintf(buf, size, "%s", test_dir);
    return t > 0 && (size_t)t < size;
  }
  const char *appdata = getenv("APPDATA");
  if (!appdata || !*appdata) return 0;
  char old[1024];
  snprintf(old, sizeof old, "%s\\" OLD_DIR_WIN, appdata);
  int n = snprintf(buf, size, "%s\\" GAME_DIR_WIN, appdata);
  if (n <= 0 || (size_t)n >= size) return 0;
  migrate(old, buf);
  return 1;
}

int plat_net_init(void) {
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

void plat_net_quit(void) { WSACleanup(); }

void plat_busy(void) { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED); }

int plat_random(void *buf, size_t size) {
  return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

const char *plat_base_path(void) {
  static char base[MAX_PATH + 1];
  if (base[0]) return base;
  DWORD n = GetModuleFileNameA(NULL, base, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return base[0] = 0, NULL;
  char *slash = strrchr(base, '\\');
  if (!slash) return base[0] = 0, NULL;
  slash[1] = 0;
  return base;
}

bool plat_is_file(const char *path) {
  DWORD a = path ? GetFileAttributesA(path) : INVALID_FILE_ATTRIBUTES;
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool plat_make_dir(const char *path) {
  char p[1024];
  int n = snprintf(p, sizeof p, "%s", path);
  if (n <= 0 || (size_t)n >= sizeof p) return false;
  for (char *c = p + 1; *c; c++) /* the folders above it first; skip the drive's own "C:\" */
    if ((*c == '\\' || *c == '/') && c[-1] != ':') {
      char was = *c;
      *c = 0;
      CreateDirectoryA(p, NULL);
      *c = was;
    }
  CreateDirectoryA(p, NULL);
  DWORD a = GetFileAttributesA(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool plat_file_sync(FILE *f) {
  if (fflush(f) != 0) return false;
  HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
  return h != INVALID_HANDLE_VALUE && FlushFileBuffers(h);
}

/* a path as the C library's fopen took it (the ANSI code page, which is UTF-8 once the program asks for it) in
 * UTF-16 */
static bool wide(const char *s, wchar_t *w, int size) { return MultiByteToWideChar(CP_ACP, 0, s, -1, w, size) > 0; }

bool plat_file_replace(const char *from, const char *to) {
  wchar_t wf[MAX_PATH * 2], wt[MAX_PATH * 2];
  if (!wide(from, wf, MAX_PATH * 2) || !wide(to, wt, MAX_PATH * 2)) return false;
  /* replaces to in one step (rename does not replace a file that is there), and returns once it is on the disk */
  return MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
