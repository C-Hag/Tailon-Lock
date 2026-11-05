/* test_win_utf8.c - Windows: the program runs with UTF-8 as its code page (the manifest win_utf8.rc embeds), so a
 * path with letters outside the system's code page goes through fopen and the A calls whole. Original code for
 * this project. */
#include <stdio.h>
#include <windows.h>

int main(void) {
  int fails = 0;
  if (GetACP() != CP_UTF8) fprintf(stderr, "test_win_utf8: the code page is %u, not UTF-8\n", GetACP()), fails++;
  const char *name = "test_win_utf8_\xc3\xa5\xc3\xa4\xc3\xb6_\xce\xb1.txt"; /* a-ring, a-umlaut, o-umlaut, alpha */
  FILE *f = fopen(name, "w");
  if (f) fputs("x", f), fclose(f);
  if (GetFileAttributesW(L"test_win_utf8_åäö_α.txt") == INVALID_FILE_ATTRIBUTES)
    fprintf(stderr, "test_win_utf8: the UTF-8 name did not make that file\n"), fails++;
  remove(name);
  if (!fails) printf("test_win_utf8: ok\n");
  return fails != 0;
}
