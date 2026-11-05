/* test_sfx.c - the sound mix asked for any number of frames at once, as CoreAudio may (2048 and more): the music
 * is never asked for more than the mix's buffer holds, and the sound is the same as in pieces of 512.
 * Original code for this project. */
#include "../src/sfx.c"

/* the music: a ramp, and the most frames it was asked for at once */
static int most;
static float tone;
void music_render(float *out, int n) {
  if (n > most) most = n;
  for (int i = 0; i < 2 * n; i++) {
    tone += 0.001f;
    out[i] += tone - floorf(tone);
  }
}
void music_restart(int theme) { (void)theme; }
void music_level(bool in_game) { (void)in_game; }
int music_theme_index(const char *key) { return (void)key, 0; }
bool plat_audio_open(int rate, PlatAudioFn fn, void *ud) { return (void)rate, (void)fn, (void)ud, false; }
void plat_audio_close(void) {}
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}
const char *plat_error(void) { return ""; }
void plat_log(const char *fmt, ...) { (void)fmt; }

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_sfx:%d: %s\n", __LINE__, #c);              \
      fails++;                                                         \
    }                                                                  \
  } while (0)

enum { N = 4096 + 300 };
static float whole[N * 2], pieces[N * 2];

static void reset(void) {
  memset(voices, 0, sizeof voices);
  memset(engines, 0, sizeof engines);
  lim_gain = 1;
  noise = 0x12345678u;
  tone = 0;
  enabled = music_on = true;
  SoundReq r = { SND_SINE, 440, 220, 0.05f, 0.8f };
  start_voice(&r);
}

int main(void) {
  reset();
  mix(whole, N, NULL);
  CHECK(most <= 512);
  reset();
  for (int at = 0; at < N; at += 512) mix(pieces + 2 * at, N - at < 512 ? N - at : 512, NULL);
  CHECK(!memcmp(whole, pieces, sizeof whole));
  if (fails) fprintf(stderr, "test_sfx: %d failed\n", fails);
  else printf("test_sfx: ok\n");
  return fails != 0;
}
