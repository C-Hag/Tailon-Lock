/* units.h - the game's units for people: what the instruments, the HUD and the menus show. The simulation
 * works in metres and metres a second; every display converts with these, so two instruments never disagree.
 * Original code for this project. */
#ifndef TL_UNITS_H
#define TL_UNITS_H

#define FT_PER_M     3.28084f   /* 1 ft = 0.3048 m exactly */
#define KT_PER_MPS   1.943844f  /* 1 kt = 1852 m an hour */
#define KMH_PER_MPS  3.6f
#define FPM_PER_MPS  196.8504f  /* feet a minute */
#define KMH_PER_KT   1.852f
#define SOUND_MPS    340.0f     /* the speed of sound the Mach meter uses */

#endif
