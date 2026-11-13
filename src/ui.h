/* ui.h - main menu, pause and game over, drawn with the bitmap font in the
 * C64 screen-and-border look of the JS overlays. */
#ifndef TL_UI_H
#define TL_UI_H

#include "config.h"
#include "core/game.h"
#include "raster.h"

typedef enum { SCREEN_MENU, SCREEN_PAUSE, SCREEN_OVER } Screen;
typedef enum { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK } Nav;
typedef enum { UI_NONE, UI_START, UI_RESUME, UI_TO_MENU, UI_QUIT, UI_CHANGED, UI_MAP, UI_THEME, UI_INTRO, UI_EDITOR, UI_GFX, UI_NET_GO, UI_NET_CANCEL } UiAction;

void     ui_open(Screen s);
UiAction ui_nav(Screen s, Settings *st, Nav n);
void     ui_draw(Framebuffer *fb, Screen s, const Settings *st, const Game *g, float time);
/* The pause box while a player's pad is away: 0 the plain pause, 1 waiting for the pad, 2 the pad back and
 * the count before the match goes on (count 3, 2, 1). */
void     ui_pad_wait(int mode, int count);
/* The mouse at (x, y) in framebuffer pixels: pointing selects what is under
 * it (true if that changed the selection); a click (how 0, or -1 / +1 for the
 * other button and the wheel) gives the step to run with ui_nav. */
bool     ui_point(float x, float y);
bool     ui_click(float x, float y, int how, Nav *n);
/* The jet the hangar page shows, or -1 when the hangar is not open. */
int      ui_hangar_jet(const Settings *st);
void     ui_open_hangar(void);
int      ui_hangar_seat(void);
/* A page of the menu other than the main one (hangar, settings, network, jet info): Esc goes back from it. */
bool     ui_sub_page(void);
/* The network page: open, and the text field being typed in (NULL if none). */
bool     ui_net_page(void);
void     ui_open_net(void);
char    *ui_text_field(Settings *st, int *size);
/* Typed text for the selected field; "\b" deletes a character. */
void     ui_text(Settings *st, const char *utf8);
/* Where the main menu's box sits on a w x h framebuffer, and its scale. */
void     ui_menu_box(int w, int h, int *x, int *y, int *bw, int *bh, int *k);
/* The LAN game picked to join. */
const char *ui_net_lan_addr(void);

#endif
