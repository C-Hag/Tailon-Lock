/* plat_win32.c - Windows side of plat.h. Keep to APIs that exist on
 * Windows 10 and older too; nothing Windows 11-only. */
#include "platform/plat.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>

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

int plat_random(void *buf, size_t size) {
  return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
