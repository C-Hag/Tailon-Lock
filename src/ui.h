/* ui.h - main menu, pause and game over, drawn with the bitmap font in the
 * C64 screen-and-border look of the JS overlays. */
#ifndef A4C_UI_H
#define A4C_UI_H

#include "config.h"
#include "core/game.h"
#include "raster.h"

typedef enum { SCREEN_MENU, SCREEN_PAUSE, SCREEN_OVER } Screen;
typedef enum { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK } Nav;
typedef enum { UI_NONE, UI_START, UI_RESUME, UI_TO_MENU, UI_QUIT, UI_CHANGED, UI_MAP, UI_THEME, UI_INTRO, UI_EDITOR, UI_GFX, UI_NET_GO, UI_NET_CANCEL } UiAction;

void     ui_open(Screen s);
UiAction ui_nav(Screen s, Settings *st, Nav n);
void     ui_draw(Framebuffer *fb, Screen s, const Settings *st, const Game *g, float time);
/* The jet the hangar page shows, or -1 when the hangar is not open. */
int      ui_hangar_jet(const Settings *st);
void     ui_open_hangar(void);
int      ui_hangar_seat(void);
/* The network page: open, and the text field being typed in (NULL if none). */
bool     ui_net_page(void);
void     ui_open_net(void);
char    *ui_text_field(Settings *st, int *size);
/* Typed text for the selected field; "\b" deletes a character. */
void     ui_text(Settings *st, const char *utf8);
/* The LAN game picked to join. */
const char *ui_net_lan_addr(void);

#endif
