/* brand.h - the game's name, in one place. Change it here (and in assets/intro/src/build_script.js, which builds the
 * intro's smoke title and title cards, and CMakeLists.txt for the executable). */
#ifndef A4C_BRAND_H
#define A4C_BRAND_H

#define GAME_TITLE    "Tailon Lock"                       /* in running text, window titles */
#define GAME_LOGO     "TAILON LOCK"                       /* logos and title cards (the bitmap font is upper case) */
#define GAME_SUBTITLE "THE GREAT NOSE2NOSE ENGAGEMENT"     /* under the logo */
#define GAME_SUBTITLE_TEXT "The Great Nose2Nose Engagement"
#define GAME_ID       "tailonlock"                        /* the executable, the config folder on Linux */
#define GAME_DIR_WIN  "TailonLock"                        /* the config folder in %APPDATA% */
#define GAME_APP_ID   "com.tailonlock.game"
/* the folders the game used before it was renamed; settings found there are copied over once */
#define OLD_ID        "ace4combat"
#define OLD_DIR_WIN   "Ace4Combat"

#endif
