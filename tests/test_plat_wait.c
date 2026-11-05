/* test_plat_wait.c - plat_wait_until from several threads at once, as the game does (the main thread paces its
 * frames, the sound thread with no sound card paces its samples): each thread waits again and again for its own
 * deadlines, and every wait must end on time, give or take a tolerance, and none may hang. The real backend's
 * timer (plat_win.c, plat_mac.m, plat_linux.c), with the test's own threads. Original code for this project.
 *
 * usage: test_plat_wait [seconds]   (default 10) */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* nanosleep, for the watchdog */
#endif
#include "platform/plat.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void nap_ms(int ms) { Sleep((DWORD)ms); }
typedef HANDLE Thread;
#define THREAD_FN(name) static DWORD WINAPI name(LPVOID arg)
static bool thread_start(Thread *t, LPTHREAD_START_ROUTINE fn, void *arg) { return (*t = CreateThread(NULL, 0, fn, arg, 0, NULL)) != NULL; }
#else
#include <pthread.h>
static void nap_ms(int ms) {
  struct timespec ts = { 0, ms * 1000000L };
  nanosleep(&ts, NULL);
}
typedef pthread_t Thread;
#define THREAD_FN(name) static void *name(void *arg)
static bool thread_start(Thread *t, void *(*fn)(void *), void *arg) { return pthread_create(t, NULL, fn, arg) == 0; }
#endif

#define THREADS 3
#define LATE_MS 50 /* a wait may end this much after its deadline (a loaded machine, a VM), never before */

typedef struct {
  int id;
  double seconds;
  atomic_int waits, early, late, done;
  double worst_ms;
} Job;

THREAD_FN(waiter) {
  Job *j = arg;
  const uint64_t hz = plat_counter_hz(), end = plat_counter() + (uint64_t)(j->seconds * (double)hz);
  /* each thread its own rhythm: 1, 3 and 7 ms, with a longer wait (150 ms) every 50th time */
  static const double STEP_MS[THREADS] = { 1, 3, 7 };
  for (int k = 0; plat_counter() < end; k++) {
    double ms = k % 50 == 49 ? 150 : STEP_MS[j->id % THREADS];
    uint64_t due = plat_counter() + (uint64_t)(ms / 1000 * (double)hz);
    plat_wait_until(due);
    uint64_t now = plat_counter();
    if (now < due) atomic_fetch_add(&j->early, 1);
    double late = now >= due ? (double)(now - due) * 1000 / (double)hz : 0;
    if (late > j->worst_ms) j->worst_ms = late;
    if (late > LATE_MS) atomic_fetch_add(&j->late, 1);
    atomic_fetch_add(&j->waits, 1);
  }
  atomic_store(&j->done, 1);
  return 0;
}

static bool on_init(int argc, char **argv, int *code) {
  double seconds = argc > 1 ? strtod(argv[1], NULL) : 10;
  if (!(seconds > 0 && seconds < 3600)) seconds = 10;
  static Job job[THREADS];
  Thread th[THREADS];
  for (int i = 0; i < THREADS; i++) {
    job[i].id = i, job[i].seconds = seconds;
    if (!thread_start(&th[i], waiter, &job[i])) {
      printf("test_plat_wait: FAIL (no thread)\n");
      *code = 1;
      return false;
    }
  }
  /* the threads are not joined: one that hangs is reported, and the process ends anyway */
  const uint64_t hz = plat_counter_hz(), give_up = plat_counter() + (uint64_t)((seconds + 10) * (double)hz);
  int finished = 0;
  while (plat_counter() < give_up && finished < THREADS) {
    finished = 0;
    for (int i = 0; i < THREADS; i++) finished += atomic_load(&job[i].done);
    nap_ms(50); /* the watchdog sleeps with the OS's own call, not the timer it is testing */
  }
  int bad = 0;
  for (int i = 0; i < THREADS; i++) {
    Job *j = &job[i];
    int hung = !atomic_load(&j->done), early = atomic_load(&j->early), late = atomic_load(&j->late);
    printf("  thread %d: %d waits, %d early, %d later than %d ms, worst %.1f ms%s\n", i, atomic_load(&j->waits), early,
           late, LATE_MS, j->worst_ms, hung ? ", HUNG" : "");
    bad += hung || early || late || atomic_load(&j->waits) == 0;
  }
#ifndef _WIN32
  /* a long wait must sleep, not spin: a 1.2 s wait on its own, and the processor time it took (clock() is the
   * process's processor time here; on Windows it is the wall time, so there this check is left out) */
  clock_t c0 = clock();
  uint64_t due = plat_counter() + hz * 6 / 5;
  plat_wait_until(due);
  double cpu = (double)(clock() - c0) / CLOCKS_PER_SEC;
  int spun = cpu > 0.1; /* a sleep takes next to none; the old macOS code spun for a sixth of the wait */
  printf("  one 1.2 s wait: %.2f s of processor time%s\n", cpu, spun ? ", SPUN instead of sleeping" : "");
  bad += spun;
#endif
  printf("test_plat_wait: %s\n", bad ? "FAIL" : "ok");
  fflush(stdout);
  if (bad) exit(1); /* exit at once: a hung thread must not keep the process alive */
  *code = 0;
  return false; /* the whole test is done here: no frames */
}

static void on_event(const PlatEvent *ev) { (void)ev; }
static bool on_frame(void) { return false; }
static int on_quit(void) { return 0; }

/* the backend's main calls these; this test runs its threads in on_init and asks for no frame */
const PlatApp *plat_app(void) {
  static const PlatApp A = { on_init, on_event, on_frame, on_quit };
  return &A;
}
