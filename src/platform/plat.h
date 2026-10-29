/* plat.h - the few things SDL3 does not cover: the settings folder and
 * socket start-up. One file per OS: plat_win32.c, plat_posix.c. */
#ifndef A4C_PLAT_H
#define A4C_PLAT_H

#include <stddef.h>

/* Writes the per-user config folder (%APPDATA%\TailonLock or
 * $XDG_CONFIG_HOME/tailonlock) into buf, copying the old Ace4Combat
 * folder's settings over the first time. Returns 0 if unknown. */
int plat_config_dir(char *buf, size_t size);

/* WSAStartup on Windows, nothing elsewhere. Returns 0 on failure. */
int  plat_net_init(void);
void plat_net_quit(void);

/* Fills buf with random bytes from the OS (keys and game codes). Returns 0 on failure. */
int  plat_random(void *buf, size_t size);

#endif
