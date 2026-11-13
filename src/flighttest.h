/* flighttest.h - the flight model test bench (--flight-test). */
#ifndef TL_FLIGHTTEST_H
#define TL_FLIGHTTEST_H

/* Flies every jet through the manoeuvres and prints the numbers; returns
 * non-zero if the physics broke anywhere. Needs the assets loaded. */
int flight_test(void);

#endif
