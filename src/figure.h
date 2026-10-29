/* figure.h - the figure engine: people as low-poly 3D meshes made in code. A skeleton is sized by Leonardo's canon
 * (the Vitruvian Man: fractions of the height), posed by joint angles and covered by lofted rings that move with
 * their bones; uniforms and kit are colours and small parts made the same way. The same engine as the browser game's
 * (assets/figure/figure.js); both make the same triangles. Original code for this project. Units: metres, y up,
 * z forward (the way the figure faces), x to the figure's left. */
#ifndef A4C_FIGURE_H
#define A4C_FIGURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  FB_HIPS, FB_CHEST, FB_NECK, FB_HEAD, FB_SHOULDER_L, FB_SHOULDER_R, FB_UPPER_L, FB_UPPER_R, FB_FORE_L, FB_FORE_R,
  FB_HAND_L, FB_HAND_R, FB_THIGH_L, FB_THIGH_R, FB_SHIN_L, FB_SHIN_R, FB_FOOT_L, FB_FOOT_R, FB_COUNT
};
/* colour roles */
enum {
  FR_TROUSERS, FR_BELT, FR_SHIRT, FR_COLLAR, FR_SLEEVE, FR_SKIN, FR_SKIN_SHADE, FR_HAIR, FR_BROW, FR_LID, FR_LIP,
  FR_EYE_WHITE, FR_IRIS, FR_MOUTH, FR_CAP, FR_GOLD, FR_KIT, FR_BOOT, FR_POCKET, FR_TAPE, FR_TEE, FR_PAPER, FR_ANCHOR,
  FR_OAK, FR_COUNT
};
enum { FP_NONE, FP_TRUNK, FP_HEAD, FP_FACE, FP_HAND_L, FP_HAND_R, FP_LIMB, FP_EYE };
enum { FU_COVERALLS, FU_KHAKI, FU_DUNGAREES };
enum { FD_NONE, FD_ANCHOR, FD_OAK };

/* a pose: per bone [bend about x, twist, spread about z] in degrees; the fingers' curl per hand; the whole figure
 * moved by root and turned by yaw */
typedef struct {
  float ang[FB_COUNT][3];
  float curl[2];
  float root[3];
  float yaw;
} FigPose;

typedef struct {
  int  uniform, device;
  bool cap, gold, headset, nametape, clipboard, rolled;
} FigKit;

/* the face's shape (0-1 each) */
typedef struct { float brow, cheek, jaw, chin, bridge, tip, lips, socket, age, stubble; } FigFace;
/* the face's expression: the jaw open, the mouth wide or round, a smile, the brows up or drawn down, the eyes
 * closing (0-1); the eyes look at gaze when has_gaze */
typedef struct { float open, wide, round, smile, brow_up, frown, blink; float gaze[3]; bool has_gaze; } FigExpr;

typedef struct {
  float   height;
  FigPose pose;
  FigKit  kit;
  uint8_t skin[3], hair[3], iris[3];
  FigFace shape;
  FigExpr face;
} FigSpec;

/* coloured by its role, by its own colour, smooth from its corners, or already lit (the room: room.h) */
enum { FS_ROLE, FS_OWN, FS_SMOOTH, FS_LIT };
typedef struct {
  float   v[3][3];
  float   ns[3][3], cs[3][3]; /* smooth: each corner's normal and colour */
  float   col[3];             /* own: its colour */
  uint8_t role, part, shade;
  uint8_t tag, emit; /* the room's: 1 + the screen drawn on it (0 none); how it shines (room.h) */
  bool    two;       /* a flat part shown from either side */
} FigTri;

/* Builds the figure's triangles into out (up to max); returns how many, or -1 if max was too small. */
int fig_build(const FigSpec *f, FigTri *out, int max);
/* The colour of a role for this figure's kit and looks. */
void fig_colour(const FigSpec *f, int role, float rgb[3]);

/* A camera: position, forward, up and right axes, focal length and centre in pixels. */
typedef struct { float pos[3], f[3], u[3], r[3], F, cx, cy; } FigCam;
/* The light: a direction toward the key light, its colour, and the rim light's colour (0-255). */
typedef struct { float dir[3], key[3], rim[3]; } FigLight;
/* a fill across a triangle: from c0 at (x0, y0) to c1 at (x1, y1), or NULL for a flat colour */
typedef struct { float x0, y0, x1, y1, c0[3], c1[3]; } FigGrad;
typedef void (*FigFill)(const float pts[3][2], const float rgb[3], const FigGrad *grad, int tag, void *ud);
/* Draws figures far to near, each facet flat-shaded (key light, fill and rim), through fill. */
void fig_draw(const FigSpec *figs, int n, const FigCam *cam, const FigLight *light, FigFill fill, void *ud);
/* a mesh built already: its triangles, and the figure they colour by role (NULL when they carry their colours) */
typedef struct { const FigTri *tris; int n; const FigSpec *spec; } FigMesh;
/* Draws meshes together, far to near, the same way; a triangle's tag goes to fill with it. */
void fig_draw_meshes(const FigMesh *m, int n, const FigCam *cam, const FigLight *light, FigFill fill, void *ud);
/* where a bone starts in the room, for the figure as posed (FB_*) */
void fig_joint(const FigSpec *f, int bone, float at[3]);

/* The canon (fractions of the height) and the same ratios measured on the built mesh (see fig_measure). */
typedef struct {
  double height, span, head, face, thirds[3], chest_top, shoulders, elbow_tip, elbow_pit, hand, foot, knee,
    knee_crotch, circle;
} FigRatios;
void fig_canon(FigRatios *r);
bool fig_measure(double H, FigRatios *r);

/* Poses: the T-pose (all zero), Leonardo's spread pose worked out from the canon, standing with the arms down, and
 * the control room's crew at their posts (raise 0-1: the commander's hand coming up for the order). */
enum { FC_OPERATOR, FC_SUPERVISOR, FC_COMMANDER };
/* the face's rules measured on the mesh (fractions, each 1 when the rule holds; see fig_face_rules) */
typedef struct { double eye_height, eye_gap, nose_width, mouth_corners, ear_top, ear_bottom; } FigFaceRules;
bool fig_face_rules(int who, FigFaceRules *r);

/* speech: a line's phonemes paced over [t0, t1], as mouth shapes, and where each word starts */
#define FIG_SEGS 128
#define FIG_WORDS 24
typedef struct {
  int   nseg, nwords;
  float t0[FIG_SEGS], t1[FIG_SEGS];
  uint8_t vis[FIG_SEGS];
  float start[FIG_WORDS];
  char  word[FIG_WORDS][16];
} FigSpeech;
void fig_speech(const char *text, float t0, float t1, FigSpeech *out);
/* the mouth at time t (open, wide, round); how many words of the caption show */
void fig_mouth(const FigSpeech *s, float t, float *open, float *wide, float *round);
int  fig_words_shown(const FigSpeech *s, float t);
/* the caption's text up to its nth spoken word, with the punctuation that follows it */
void fig_spoken_upto(const char *text, int n, char *out, size_t size);
void fig_pose_vitruvian(float H, FigPose *p);
void fig_pose_stand(FigPose *p);
void fig_crew(int who, float H, float raise, FigSpec *out);

#endif
