/* loader.c - the floppy's start program: unpacks game.tlpk from its own folder to a temporary folder, plays it with the
 * disk's assets (A4C_ASSETS), and removes the copy. Original code for this project. A tool, not game code: the game
 * itself keeps its system calls in src/platform. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE /* mkdtemp, setenv and readlink, under -std=c11 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff/tlpk.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SEP '\\'
#define GAME_NAME "tailonlock.exe"
#else
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define SEP '/'
#define GAME_NAME "tailonlock"
#endif

#define PATH_LEN 4096

/* on Windows the program has no console: the message in a box */
static int fail(const char *what) {
#ifdef _WIN32
  wchar_t w[256];
  if (MultiByteToWideChar(CP_UTF8, 0, what, -1, w, 256) > 0) MessageBoxW(NULL, w, L"Tailon Lock", MB_OK | MB_ICONERROR);
#else
  fprintf(stderr, "tailonlock: %s\n", what);
#endif
  return 1;
}

/* this program's folder, with the separator at its end */
static int own_folder(char *out, size_t size) {
#ifdef _WIN32
  DWORD n = GetModuleFileNameA(NULL, out, (DWORD)size);
  if (n == 0 || n >= size) return 0;
#elif defined(__APPLE__)
  uint32_t n = (uint32_t)size;
  if (_NSGetExecutablePath(out, &n) != 0) return 0;
#else
  ssize_t n = readlink("/proc/self/exe", out, size - 1);
  if (n <= 0) return 0;
  out[n] = 0;
#endif
  char *end = strrchr(out, SEP);
  if (!end) return 0;
  end[1] = 0;
  return 1;
}

static unsigned char *read_all(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  unsigned char *b = NULL;
  long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
  if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (b = malloc((size_t)n)) != NULL) {
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
      free(b);
      b = NULL;
    }
    *size = (size_t)n;
  }
  fclose(f);
  return b;
}

static int write_all(const char *path, const unsigned char *b, size_t n) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  int ok = fwrite(b, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}

int main(int argc, char **argv) {
  char dir[PATH_LEN], src[PATH_LEN + 16], assets[PATH_LEN + 16], tmp[PATH_LEN], game[PATH_LEN + 32];
  if (!own_folder(dir, sizeof dir)) return fail("cannot find my own folder");
  snprintf(src, sizeof src, "%sgame.tlpk", dir);
  snprintf(assets, sizeof assets, "%sassets%c", dir, SEP);
  size_t packed = 0, raw = 0;
  unsigned char *b = read_all(src, &packed);
  if (!b) return fail("cannot read game.tlpk");
  unsigned char *u = tlpk_is(b, packed) ? tlpk_unpack(b, packed, (size_t)64 << 20, &raw) : NULL;
  free(b);
  if (!u) return fail("game.tlpk is damaged");
#ifdef _WIN32
  char base[MAX_PATH];
  if (!GetTempPathA(sizeof base, base)) return fail("no temporary folder");
  snprintf(tmp, sizeof tmp, "%stailonlock-%lu", base, (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(tmp, NULL)) return fail("cannot make a temporary folder");
#else
  const char *base = getenv("TMPDIR");
  snprintf(tmp, sizeof tmp, "%s/tailonlock-XXXXXX", base && *base ? base : "/tmp");
  if (!mkdtemp(tmp)) return fail("cannot make a temporary folder");
#endif
  snprintf(game, sizeof game, "%s%c%s", tmp, SEP, GAME_NAME);
  int ok = write_all(game, u, raw);
  free(u);
  int code = 1;
  if (!ok) {
    fail("cannot write the unpacked game");
  } else {
#ifdef _WIN32
    SetEnvironmentVariableA("A4C_ASSETS", assets);
    char line[32768];
    size_t at = (size_t)snprintf(line, sizeof line, "\"%s\"", game);
    for (int i = 1; i < argc && at + strlen(argv[i]) + 4 < sizeof line; i++) at += (size_t)snprintf(line + at, sizeof line - at, " \"%s\"", argv[i]);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (CreateProcessA(game, line, NULL, NULL, TRUE, 0, NULL, tmp, &si, &pi)) {
      DWORD rc = 1;
      WaitForSingleObject(pi.hProcess, INFINITE);
      GetExitCodeProcess(pi.hProcess, &rc);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      code = (int)rc;
    } else {
      fail("cannot start the game");
    }
#else
    setenv("A4C_ASSETS", assets, 1);
    /* the game's own arguments, with its name first */
    char **args = (char **)calloc((size_t)argc + 1, sizeof *args);
    pid_t pid = args && chmod(game, 0700) == 0 ? fork() : -1;
    if (pid == 0) {
      args[0] = game;
      for (int i = 1; i < argc; i++) args[i] = argv[i];
      execv(game, args);
      _exit(127);
    }
    free((void *)args);
    int st = 0;
    if (pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st)) code = WEXITSTATUS(st);
    else fail("cannot start the game");
#endif
  }
#ifdef _WIN32
  /* the file stays locked a moment after the game has gone */
  for (int i = 0; i < 20 && !DeleteFileA(game); i++) Sleep(250);
  RemoveDirectoryA(tmp);
#else
  remove(game);
  rmdir(tmp);
#endif
  return code;
}
