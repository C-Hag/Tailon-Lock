/* doctrinetest.h - measures behind the CPU pilots' doctrine (--doctrine-measure). */
#ifndef A4C_DOCTRINETEST_H
#define A4C_DOCTRINETEST_H

/* Prints what the simulation gives for the doctrine's questions, without
 * changing it: speed back after a post-stall move or a nose-high slow-down,
 * the radar lock's reach against each jet, the lock cone, each missile
 * against a turn and decoys, and how long each jet's fuel lasts in a fight.
 * Needs the assets loaded; returns 0. */
int doctrine_measure(void);

#endif
