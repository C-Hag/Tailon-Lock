/* SDL.h - a stand-in for the few SDL3 calls the renderer, sim, intro and
 * synth make, so they build as a plain static program for the Pentium
 * benchmark (tools/p5bench). Test tool only, never shipped. Original code
 * for this project; the names are the ones our sources call. */
#ifndef P5_SDL_STUB_H
#define P5_SDL_STUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SDLCALL
#define SDL_PIXELFORMAT_XRGB8888 0
#define SDL_AUDIO_F32 1
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0

typedef struct SDL_Window SDL_Window;
typedef struct SDL_IOStream SDL_IOStream;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef struct { int format, channels, freq; } SDL_AudioSpec;
typedef void (*SDL_AudioStreamCallback)(void *ud, SDL_AudioStream *s, int additional, int total);
typedef enum { SDL_PATHTYPE_NONE, SDL_PATHTYPE_FILE, SDL_PATHTYPE_DIRECTORY, SDL_PATHTYPE_OTHER } SDL_PathType;
typedef struct { SDL_PathType type; uint64_t size; } SDL_PathInfo;

void        SDL_Log(const char *fmt, ...);
void        SDL_free(void *p);
void       *SDL_LoadFile(const char *path, size_t *size);
const char *SDL_GetBasePath(void);
const char *SDL_GetError(void);
bool        SDL_GetPathInfo(const char *path, SDL_PathInfo *info);
SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode);
size_t      SDL_WriteIO(SDL_IOStream *f, const void *p, size_t n);
bool        SDL_CloseIO(SDL_IOStream *f);
SDL_AudioStream *SDL_OpenAudioDeviceStream(int dev, const SDL_AudioSpec *spec, SDL_AudioStreamCallback cb, void *ud);
bool        SDL_ResumeAudioStreamDevice(SDL_AudioStream *s);
void        SDL_DestroyAudioStream(SDL_AudioStream *s);
bool        SDL_LockAudioStream(SDL_AudioStream *s);
bool        SDL_UnlockAudioStream(SDL_AudioStream *s);
bool        SDL_PutAudioStreamData(SDL_AudioStream *s, const void *p, int n);

/* the benchmark pulls n frames of the sound-effect mix through the stored callback */
void p5_pull_audio(int frames);

#endif
