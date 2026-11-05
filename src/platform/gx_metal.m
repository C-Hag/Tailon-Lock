/* gx_metal.m - the GPU device of gx.h on Apple's Metal, for the modern renderer (gpu.c) on macOS. Original code for
 * this project, written from Apple's Metal documentation (MTLDevice, MTLCommandQueue, MTLRenderCommandEncoder,
 * MTLBlitCommandEncoder, CAMetalLayer).
 *
 * The window's picture is the CAMetalLayer plat_mac.m made (PlatNative.display); headless (no window) the picture
 * is a texture of its own. Shaders: shaders/metal.msl, one source compiled once at start, each entry point by name;
 * the uniforms of gx.h's slots 0..1 are pushed by value into [[buffer(0..1)]], the one vertex buffer goes to
 * buffer VB_INDEX (the pipeline's vertex descriptor, [[stage_in]] in the shader). Clip space is the same as
 * Direct3D's, so nothing is flipped.
 *
 * Uploads are copied at once into this frame's staging memory (shared with the GPU) and blitted into the private
 * buffer or texture before the frame's first pass, so nothing the GPU may still be reading for an earlier frame is
 * written over. Up to FRAMES frames are in flight. */
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <objc/objc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/gx.h"
#include "platform/plat.h"

#define FRAMES   3

/* the frame's own autorelease pool (the command buffer, the drawable and the pass descriptors Metal hands back
 * autoreleased): pushed in gx_frame_begin and popped in gx_frame_end, so they go every frame, not at exit */
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *pool);
static void *frame_pool;
#define VB_INDEX 30

struct GxTex {
  id<MTLTexture> t;
  int w, h, samples;
  GxFormat f;
};
struct GxBuf {
  id<MTLBuffer> b;
  uint32_t size;
};
struct GxSampler {
  id<MTLSamplerState> s;
};
struct GxPipe {
  id<MTLRenderPipelineState> ps;
  id<MTLDepthStencilState> ds;
  MTLPrimitiveType prim;
  bool bias;
};

static id<MTLDevice>        dev;
static id<MTLCommandQueue>  queue;
static id<MTLLibrary>       lib;
static const void          *lib_src; /* the source lib was made from */
static CAMetalLayer        *layer;
static GxTex                offscreen;
static char                 err[256];
static dispatch_semaphore_t inflight;
static bool                 mailbox;

/* the frame */
static id<MTLCommandBuffer>        cmd;
static id<CAMetalDrawable>         drawable;
static GxTex                       swap;
static id<MTLRenderCommandEncoder> enc;
static GxPipe                     *bound;
static GxBuf                      *index_buf;
static int                         pass_w, pass_h;

/* staging: one ring per frame in flight, and the copies to make before the first pass */
static id<MTLBuffer> stage[FRAMES];
static size_t        stage_used;
static int           slot;
typedef struct { size_t from; uint32_t size; GxBuf *b; GxTex *t; int w, h; } Copy;
static Copy  *copies;
static int    ncopies, capcopies;

/* readback */
static id<MTLBuffer> rb_buf;
static void         *rb_out;
static int           rb_w, rb_h;

static void fail(const char *what, NSError *e) {
  snprintf(err, sizeof err, "%s%s%s", what, e ? ": " : "", e ? e.localizedDescription.UTF8String : "");
}

const char *gx_error(void) { return err; }
const char *gx_driver(void) { return "metal"; }
GxShaderKind gx_shader_kind(void) { return GX_MSL; }
GxFormat gx_swap_format(void) { return GX_BGRA8; }
bool gx_mailbox(void) { return mailbox; }

static MTLPixelFormat fmt_of(GxFormat f) {
  switch (f) {
  case GX_RGBA16F: return MTLPixelFormatRGBA16Float;
  case GX_D32F: return MTLPixelFormatDepth32Float;
  case GX_RGBA8: return MTLPixelFormatRGBA8Unorm;
  default: return MTLPixelFormatBGRA8Unorm;
  }
}

bool gx_supports_samples(GxFormat f, int n) {
  (void)f;
  return dev && [dev supportsTextureSampleCount:(NSUInteger)n];
}

bool gx_open(const void *native_window, int w, int h, bool vsync) {
  const PlatNative *nw = native_window;
  err[0] = 0;
  if (nw && nw->kind == PNAT_COCOA && nw->display) {
    layer = (__bridge CAMetalLayer *)nw->display;
    dev = layer.device ? layer.device : MTLCreateSystemDefaultDevice();
    layer.device = dev;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.maximumDrawableCount = 3;
    /* no display sync: the newest picture shows at the next refresh, the way mailbox presenting does; gpu.c paces
     * the frames itself */
    layer.displaySyncEnabled = vsync;
    mailbox = !vsync;
  } else {
    dev = MTLCreateSystemDefaultDevice();
    layer = nil;
    mailbox = false;
  }
  if (!dev) {
    fail("no Metal device", nil);
    return false;
  }
  queue = [dev newCommandQueue];
  inflight = dispatch_semaphore_create(FRAMES);
  if (!layer) { /* headless: the picture is a texture of our own */
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                 width:(NSUInteger)(w < 1 ? 1 : w)
                                                                                height:(NSUInteger)(h < 1 ? 1 : h)
                                                                             mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    offscreen.t = [dev newTextureWithDescriptor:d];
    offscreen.w = w < 1 ? 1 : w, offscreen.h = h < 1 ? 1 : h, offscreen.samples = 1, offscreen.f = GX_BGRA8;
  }
  return queue != nil;
}

void gx_wait_idle(void) {
  if (!queue) return;
  id<MTLCommandBuffer> c = [queue commandBuffer];
  [c commit];
  [c waitUntilCompleted];
}

void gx_close(void) {
  gx_wait_idle();
  for (int i = 0; i < FRAMES; i++) stage[i] = nil;
  free(copies);
  copies = NULL, ncopies = capcopies = 0;
  offscreen.t = nil;
  rb_buf = nil;
  lib = nil, lib_src = NULL, queue = nil, dev = nil, layer = nil, inflight = nil;
}

/* ---- resources ---- */

GxTex *gx_texture(GxFormat f, int w, int h, int samples, unsigned usage) {
  MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt_of(f)
                                                                               width:(NSUInteger)w
                                                                              height:(NSUInteger)h
                                                                           mipmapped:NO];
  if (samples > 1) {
    d.textureType = MTLTextureType2DMultisample;
    d.sampleCount = (NSUInteger)samples;
  }
  d.storageMode = MTLStorageModePrivate;
  d.usage = (usage & GX_TEX_SAMPLED ? MTLTextureUsageShaderRead : 0) |
            (usage & (GX_TEX_COLOR | GX_TEX_DEPTH) ? MTLTextureUsageRenderTarget : 0);
  if (!d.usage) d.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> t = [dev newTextureWithDescriptor:d];
  if (!t) {
    fail("cannot make a texture", nil);
    return NULL;
  }
  GxTex *x = calloc(1, sizeof *x);
  x->t = t, x->w = w, x->h = h, x->samples = samples, x->f = f;
  return x;
}

void gx_texture_free(GxTex *t) {
  if (!t) return;
  t->t = nil;
  free(t);
}

GxBuf *gx_buffer(unsigned usage, uint32_t size) {
  (void)usage;
  id<MTLBuffer> b = [dev newBufferWithLength:size ? size : 16 options:MTLResourceStorageModePrivate];
  if (!b) {
    fail("cannot make a buffer", nil);
    return NULL;
  }
  GxBuf *x = calloc(1, sizeof *x);
  x->b = b, x->size = size;
  return x;
}

void gx_buffer_free(GxBuf *b) {
  if (!b) return;
  b->b = nil;
  free(b);
}

GxSampler *gx_sampler(bool linear, bool compare) {
  MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
  d.minFilter = d.magFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  d.sAddressMode = d.tAddressMode = MTLSamplerAddressModeClampToEdge;
  if (compare) d.compareFunction = MTLCompareFunctionLessEqual;
  GxSampler *s = calloc(1, sizeof *s);
  s->s = [dev newSamplerStateWithDescriptor:d];
  return s;
}

void gx_sampler_free(GxSampler *s) {
  if (!s) return;
  s->s = nil;
  free(s);
}

static id<MTLFunction> function(const GxPipeDesc *d, bool vertex) {
  const void *code = vertex ? d->vs_code : d->fs_code;
  size_t size = vertex ? d->vs_size : d->fs_size;
  if (!lib || lib_src != code) { /* the source is one for every shader: compiled once */
    NSString *src = [[NSString alloc] initWithBytes:code length:size encoding:NSUTF8StringEncoding];
    NSError *e = nil;
    MTLCompileOptions *o = [MTLCompileOptions new];
    o.fastMathEnabled = NO; /* the same maths as the other GPUs and the software renderer */
    lib = [dev newLibraryWithSource:src options:o error:&e];
    lib_src = lib ? code : NULL;
    if (!lib) {
      fail("the shaders do not compile", e);
      return nil;
    }
  }
  id<MTLFunction> f = [lib newFunctionWithName:@(vertex ? d->vs_entry : d->fs_entry)];
  if (!f) snprintf(err, sizeof err, "no shader %s", vertex ? d->vs_entry : d->fs_entry);
  return f;
}

static MTLVertexFormat attr_fmt(GxAttrFormat f) {
  return f == GX_F32x3 ? MTLVertexFormatFloat3 : f == GX_F32x4 ? MTLVertexFormatFloat4 : MTLVertexFormatUChar4Normalized;
}

GxPipe *gx_pipeline(const GxPipeDesc *d) {
  MTLRenderPipelineDescriptor *p = [MTLRenderPipelineDescriptor new];
  p.vertexFunction = function(d, true);
  p.fragmentFunction = function(d, false);
  if (!p.vertexFunction || !p.fragmentFunction) return NULL;
  if (d->stride) {
    MTLVertexDescriptor *v = [MTLVertexDescriptor vertexDescriptor];
    for (int i = 0; i < d->nattrs; i++) {
      v.attributes[(NSUInteger)d->attrs[i].location].format = attr_fmt(d->attrs[i].format);
      v.attributes[(NSUInteger)d->attrs[i].location].offset = d->attrs[i].offset;
      v.attributes[(NSUInteger)d->attrs[i].location].bufferIndex = VB_INDEX;
    }
    v.layouts[VB_INDEX].stride = d->stride;
    v.layouts[VB_INDEX].stepFunction = d->per_instance ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
    p.vertexDescriptor = v;
  }
  if (d->has_color) {
    MTLRenderPipelineColorAttachmentDescriptor *c = p.colorAttachments[0];
    c.pixelFormat = fmt_of(d->color_format);
    if (d->blend_premul) {
      c.blendingEnabled = YES;
      c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = MTLBlendFactorOne;
      c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
  }
  if (d->has_depth) p.depthAttachmentPixelFormat = fmt_of(d->depth_format);
  p.rasterSampleCount = (NSUInteger)(d->samples > 1 ? d->samples : 1);
  p.inputPrimitiveTopology = d->prim == GX_LINES ? MTLPrimitiveTopologyClassLine : MTLPrimitiveTopologyClassTriangle;
  NSError *e = nil;
  id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:p error:&e];
  if (!ps) {
    fail("pipeline", e);
    return NULL;
  }
  MTLDepthStencilDescriptor *z = [MTLDepthStencilDescriptor new];
  z.depthCompareFunction = !d->depth_test ? MTLCompareFunctionAlways
                           : d->depth_less_equal ? MTLCompareFunctionLessEqual : MTLCompareFunctionGreaterEqual;
  z.depthWriteEnabled = d->depth_write;
  GxPipe *x = calloc(1, sizeof *x);
  x->ps = ps;
  x->ds = [dev newDepthStencilStateWithDescriptor:z];
  x->prim = d->prim == GX_LINES ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle;
  x->bias = d->depth_bias;
  return x;
}

void gx_pipeline_free(GxPipe *p) {
  if (!p) return;
  p->ps = nil, p->ds = nil;
  free(p);
}

/* ---- the frame ---- */

bool gx_frame_begin(GxTex **out, int *w, int *h) {
  dispatch_semaphore_wait(inflight, DISPATCH_TIME_FOREVER);
  frame_pool = objc_autoreleasePoolPush();
  slot = (slot + 1) % FRAMES;
  stage_used = 0;
  ncopies = 0;
  cmd = [queue commandBuffer];
  if (layer) {
    drawable = [layer nextDrawable];
    if (!drawable) {
      cmd = nil;
      objc_autoreleasePoolPop(frame_pool);
      frame_pool = NULL;
      dispatch_semaphore_signal(inflight);
      return false;
    }
    swap.t = drawable.texture;
    swap.w = (int)drawable.texture.width, swap.h = (int)drawable.texture.height, swap.samples = 1, swap.f = GX_BGRA8;
    *out = &swap;
  } else {
    *out = &offscreen;
  }
  *w = (*out)->w, *h = (*out)->h;
  return true;
}

/* room in this frame's staging memory */
static size_t stage_take(uint32_t size) {
  size_t at = (stage_used + 255) & ~(size_t)255;
  if (!stage[slot] || at + size > stage[slot].length) {
    /* grow: a new, larger ring for this slot (the copies already recorded keep their bytes in the old one, so
     * the frame's copies are run first) */
    size_t want = (at + size) * 2 < (1u << 22) ? 1u << 22 : (at + size) * 2;
    if (stage[slot] && ncopies) {
      id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
      for (int i = 0; i < ncopies; i++) {
        Copy *c = &copies[i];
        if (c->b) [b copyFromBuffer:stage[slot] sourceOffset:c->from toBuffer:c->b->b destinationOffset:0 size:c->size];
        else
          [b copyFromBuffer:stage[slot] sourceOffset:c->from sourceBytesPerRow:(NSUInteger)c->w * 4
              sourceBytesPerImage:(NSUInteger)c->w * 4 * (NSUInteger)c->h sourceSize:MTLSizeMake((NSUInteger)c->w, (NSUInteger)c->h, 1)
                        toTexture:c->t->t destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
      }
      [b endEncoding];
      ncopies = 0;
    }
    stage[slot] = [dev newBufferWithLength:want options:MTLResourceStorageModeShared];
    at = 0;
  }
  stage_used = at + size;
  return at;
}

/* a slot for one more copy, or NULL when out of memory (that upload is then dropped, not the ones before it) */
static Copy *copy_add(void) {
  if (ncopies == capcopies) {
    int cap = capcopies ? capcopies * 2 : 32;
    Copy *c = realloc(copies, (size_t)cap * sizeof *copies);
    if (!c) return NULL;
    copies = c;
    capcopies = cap;
  }
  return &copies[ncopies++];
}

void gx_upload_buffer(GxBuf *b, const void *data, uint32_t size) {
  if (!b || !size) return;
  if (size > b->size) size = b->size;
  if (!cmd) { /* outside a frame: at once */
    id<MTLBuffer> s = [dev newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
    if (!s) return; /* out of memory: no upload, rather than a nil source for the blit */
    id<MTLCommandBuffer> c = [queue commandBuffer];
    id<MTLBlitCommandEncoder> e = [c blitCommandEncoder];
    [e copyFromBuffer:s sourceOffset:0 toBuffer:b->b destinationOffset:0 size:size];
    [e endEncoding];
    [c commit];
    [c waitUntilCompleted];
    return;
  }
  size_t at = stage_take(size);
  memcpy((uint8_t *)stage[slot].contents + at, data, size);
  Copy *c = copy_add();
  if (c) *c = (Copy){ at, size, b, NULL, 0, 0 };
}

void gx_upload_texture(GxTex *t, const void *px, int w, int h) {
  if (!t || !cmd) return;
  uint32_t size = (uint32_t)w * (uint32_t)h * 4;
  size_t at = stage_take(size);
  memcpy((uint8_t *)stage[slot].contents + at, px, size);
  Copy *c = copy_add();
  if (c) *c = (Copy){ at, size, NULL, t, w, h };
}

static void run_copies(void) {
  if (!ncopies) return;
  id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
  for (int i = 0; i < ncopies; i++) {
    Copy *c = &copies[i];
    if (c->b) [b copyFromBuffer:stage[slot] sourceOffset:c->from toBuffer:c->b->b destinationOffset:0 size:c->size];
    else
      [b copyFromBuffer:stage[slot] sourceOffset:c->from sourceBytesPerRow:(NSUInteger)c->w * 4
          sourceBytesPerImage:(NSUInteger)c->size sourceSize:MTLSizeMake((NSUInteger)c->w, (NSUInteger)c->h, 1)
                    toTexture:c->t->t destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
  }
  [b endEncoding];
  ncopies = 0;
}

static MTLLoadAction load_of(GxLoad l) {
  return l == GX_LOAD_CLEAR ? MTLLoadActionClear : l == GX_LOAD ? MTLLoadActionLoad : MTLLoadActionDontCare;
}

void gx_pass_begin(const GxPass *p) {
  run_copies(); /* the uploads come before the first pass */
  MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
  pass_w = pass_h = 0;
  if (p->color) {
    MTLRenderPassColorAttachmentDescriptor *c = rp.colorAttachments[0];
    c.texture = p->color->t;
    c.loadAction = load_of(p->color_load);
    c.clearColor = MTLClearColorMake(p->clear[0], p->clear[1], p->clear[2], p->clear[3]);
    if (p->resolve) {
      c.resolveTexture = p->resolve->t;
      c.storeAction = MTLStoreActionStoreAndMultisampleResolve; /* the next view of the layer loads it */
    } else {
      c.storeAction = MTLStoreActionStore;
    }
    pass_w = p->color->w, pass_h = p->color->h;
  }
  if (p->depth) {
    MTLRenderPassDepthAttachmentDescriptor *z = rp.depthAttachment;
    z.texture = p->depth->t;
    z.loadAction = load_of(p->depth_load);
    z.clearDepth = p->clear_depth;
    z.storeAction = p->depth_store ? MTLStoreActionStore : MTLStoreActionDontCare;
    if (!pass_w) pass_w = p->depth->w, pass_h = p->depth->h;
  }
  enc = [cmd renderCommandEncoderWithDescriptor:rp];
  bound = NULL;
}

void gx_pass_end(void) {
  [enc endEncoding];
  enc = nil;
  bound = NULL;
}

void gx_bind_pipeline(GxPipe *p) {
  if (!p || !enc) return;
  [enc setRenderPipelineState:p->ps];
  [enc setDepthStencilState:p->ds];
  if (p->bias) [enc setDepthBias:2 slopeScale:2.5f clamp:0];
  else [enc setDepthBias:0 slopeScale:0 clamp:0];
  bound = p;
}

void gx_viewport(float x, float y, float w, float h) {
  [enc setViewport:(MTLViewport){ x, y, w, h, 0, 1 }];
}

void gx_scissor(int x, int y, int w, int h) {
  /* Metal wants the rectangle inside the target */
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > pass_w) w = pass_w - x;
  if (y + h > pass_h) h = pass_h - y;
  if (w < 1 || h < 1) x = y = 0, w = h = 1;
  [enc setScissorRect:(MTLScissorRect){ (NSUInteger)x, (NSUInteger)y, (NSUInteger)w, (NSUInteger)h }];
}

void gx_bind_vertex(GxBuf *b) {
  if (b) [enc setVertexBuffer:b->b offset:0 atIndex:VB_INDEX];
}

void gx_bind_index32(GxBuf *b) { index_buf = b; }

void gx_bind_textures(GxTex *const *t, GxSampler *const *s, int n) {
  for (int i = 0; i < n; i++) {
    [enc setFragmentTexture:t[i] ? t[i]->t : nil atIndex:(NSUInteger)i];
    [enc setFragmentSamplerState:s[i] ? s[i]->s : nil atIndex:(NSUInteger)i];
  }
}

void gx_uniform(int stage_, int slot_, const void *data, uint32_t size) {
  if (!enc) return;
  if (stage_ == 0) [enc setVertexBytes:data length:size atIndex:(NSUInteger)slot_];
  else [enc setFragmentBytes:data length:size atIndex:(NSUInteger)slot_];
}

void gx_draw(uint32_t verts, uint32_t instances, uint32_t first_vert, uint32_t first_instance) {
  if (!bound || !verts || !instances) return;
  [enc drawPrimitives:bound->prim vertexStart:first_vert vertexCount:verts instanceCount:instances
         baseInstance:first_instance];
}

void gx_draw_indexed(uint32_t indices) {
  if (!bound || !index_buf || !indices) return;
  [enc drawIndexedPrimitives:bound->prim indexCount:indices indexType:MTLIndexTypeUInt32 indexBuffer:index_buf->b
           indexBufferOffset:0];
}

void gx_readback(GxTex *t, void *out) {
  if (!t || !out || !cmd) return;
  NSUInteger bytes = (NSUInteger)t->w * (NSUInteger)t->h * 4;
  if (!rb_buf || rb_buf.length < bytes) rb_buf = [dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
  if (!rb_buf) return; /* out of memory: no readback (out keeps what it had) */
  id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
  [b copyFromTexture:t->t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
          sourceSize:MTLSizeMake((NSUInteger)t->w, (NSUInteger)t->h, 1) toBuffer:rb_buf destinationOffset:0
  destinationBytesPerRow:(NSUInteger)t->w * 4 destinationBytesPerImage:bytes];
  [b endEncoding];
  rb_out = out, rb_w = t->w, rb_h = t->h;
}

void gx_frame_end(void) {
  if (!cmd) return;
  run_copies();
  if (drawable) [cmd presentDrawable:drawable];
  dispatch_semaphore_t sem = inflight;
  [cmd addCompletedHandler:^(id<MTLCommandBuffer> c) {
    (void)c;
    dispatch_semaphore_signal(sem);
  }];
  [cmd commit];
  if (rb_out) { /* a picture asked for: wait for it */
    [cmd waitUntilCompleted];
    memcpy(rb_out, rb_buf.contents, (size_t)rb_w * (size_t)rb_h * 4);
    rb_out = NULL;
  }
  if (cmd.status == MTLCommandBufferStatusError) fail("the frame failed on the GPU", cmd.error);
  cmd = nil, drawable = nil, swap.t = nil;
  if (frame_pool) objc_autoreleasePoolPop(frame_pool);
  frame_pool = NULL;
}
