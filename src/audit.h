/* audit.h - the flight and game audit (--audit): the jets against their
 * data sheets, the systems a pilot uses, and whole matches per plane. */
#ifndef TL_AUDIT_H
#define TL_AUDIT_H

/* Runs the parts the arguments ask for; returns non-zero if a check
 * failed. Needs the assets loaded. */
int audit(int argc, char **argv);

#endif
