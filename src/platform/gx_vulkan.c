/* gx_vulkan.c - the GPU device (gx.h) on Vulkan 1.3 with dynamic rendering,
 * through the system's loader opened at run time. Original code for this
 * project, written from the Vulkan 1.3 specification (Khronos) and the
 * VK_KHR_surface, VK_KHR_swapchain, VK_KHR_wayland_surface,
 * VK_KHR_xlib_surface and VK_KHR_win32_surface extension specifications.
 *
 * Two frames in flight, each with its command buffer, fence, uniform ring,
 * descriptor pool and upload staging. Uniform data goes to the ring and is
 * bound with dynamic offsets; textures get a descriptor set per bind. Every
 * image knows its layout and is moved with a barrier when its use changes;
 * a target that will be sampled is moved to the read layout when its pass
 * ends, so no barrier is ever needed inside a pass. Freed resources wait
 * until the frames that may use them are done. */
#include "platform/gx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/plat.h"
#include "platform/vk_api.h"

#ifdef _WIN32
#include <windows.h>
#define LIB_OPEN() ((void *)LoadLibraryA("vulkan-1.dll"))
#define LIB_SYM(l, n) ((void *)GetProcAddress((HMODULE)(l), n))
#else
#include <dlfcn.h>
#define LIB_OPEN() dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)
#define LIB_SYM(l, n) dlsym(l, n)
#endif

#define X(n, r, a) static r(VKAPI *n) a;
VK_GLOBAL_CALLS(X)
VK_INSTANCE_CALLS(X)
VK_DEVICE_CALLS(X)
#undef X
static VkResult(VKAPI *vkCreateWaylandSurfaceKHR)(VkInstance, const VkWaylandSurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
static VkResult(VKAPI *vkCreateXlibSurfaceKHR)(VkInstance, const VkXlibSurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
static VkResult(VKAPI *vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
static PFN_vkGetInstanceProcAddr gipa;

#define FRAMES 2
#define RING (4u << 20) /* uniform data per frame */
#define UALIGN 256u     /* the most minUniformBufferOffsetAlignment may be */
#define MAXSWAP 8

struct GxTex {
  VkImage img;
  VkImageView view;
  VkDeviceMemory mem;
  int fmt, layout, w, h, samples;
  unsigned usage;
  bool depth, swap;
};
struct GxBuf { VkBuffer buf; VkDeviceMemory mem; uint32_t size; };
struct GxPipe { VkPipeline p; };
struct GxSampler { VkSampler s; };

typedef struct { int kind; uint64_t a, b, c; uint64_t frame; } Dead; /* a resource freed, with the frame that last could use it */
enum { DEAD_IMAGE, DEAD_BUFFER, DEAD_PIPELINE, DEAD_SAMPLER };

typedef struct {
  VkCommandBuffer cmd;
  VkFence fence;
  VkSemaphore acquired;
  GxBuf ring;
  uint8_t *ring_map;
  uint32_t ring_used;
  VkDescriptorSet uset; /* the ring's uniform buffers, sets 1 and 3 */
  VkDescriptorPool pool; /* textures, reset each frame */
  GxBuf stage;
  uint8_t *stage_map;
  uint32_t stage_used;
  uint64_t number; /* the frame it last held */
} Frame;

static void *lib;
static VkInstance inst;
static VkPhysicalDevice phys;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;
static VkPhysicalDeviceMemoryProperties memprops;
static VkSurfaceKHR surface;
static VkSwapchainKHR chain;
static VkImage chain_img[MAXSWAP];
static GxTex chain_tex[MAXSWAP];
static VkSemaphore chain_done[MAXSWAP];
static uint32_t nchain, chain_index;
static int chain_fmt = VK_FORMAT_B8G8R8A8_UNORM, chain_w, chain_h;
static bool has_mailbox, want_vsync, chain_bad;
static const PlatNative *native;
static GxTex *offscreen; /* the picture when there is no window */
static VkCommandPool cpool;
static VkDescriptorSetLayout lay_empty, lay_ubo_vs, lay_tex, lay_ubo_fs;
static VkPipelineLayout playout;
static Frame frames[FRAMES];
static int fi;               /* the frame being recorded */
static uint64_t frame_no = 1; /* its number; frames before done_no are done */
static uint64_t done_no;
static Dead *dead;
static int ndead, capdead;
static char err[256];
static char drvname[64] = "vulkan";

/* the frame's state while recording */
static VkCommandBuffer cmd;
static bool in_pass;
static uint32_t uoff[2][2]; /* stage, slot -> offset in the ring */
static bool udirty;
static struct { GxTex *t; void *out; GxBuf buf; } rb;

static void fail(const char *what, VkResult r) { snprintf(err, sizeof err, "%s failed (%d)", what, (int)r); }
const char *gx_error(void) { return err; }
const char *gx_driver(void) { return drvname; }
GxShaderKind gx_shader_kind(void) { return GX_SPIRV; }
GxFormat gx_swap_format(void) { return chain_fmt == VK_FORMAT_R8G8B8A8_UNORM ? GX_RGBA8 : GX_BGRA8; }
bool gx_mailbox(void) { return has_mailbox; }
void gx_wait_idle(void) {
  if (dev) vkDeviceWaitIdle(dev);
}

static int vk_format(GxFormat f) {
  switch (f) {
  case GX_RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
  case GX_D32F: return VK_FORMAT_D32_SFLOAT;
  case GX_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
  default: return VK_FORMAT_B8G8R8A8_UNORM;
  }
}

/* ---- memory --------------------------------------------------------------------- */
static int mem_type(uint32_t bits, VkFlags want) {
  for (uint32_t i = 0; i < memprops.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want) return (int)i;
  return -1;
}

static bool alloc(VkMemoryRequirements *rq, VkFlags want, VkFlags fallback, VkDeviceMemory *mem) {
  int t = mem_type(rq->memoryTypeBits, want);
  if (t < 0) t = mem_type(rq->memoryTypeBits, fallback);
  if (t < 0) return fail("no memory type", 0), false;
  VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, rq->size, (uint32_t)t };
  VkResult r = vkAllocateMemory(dev, &ai, NULL, mem);
  if (r) fail("vkAllocateMemory", r);
  return r == VK_SUCCESS;
}

static bool make_buffer(GxBuf *b, VkFlags usage, uint32_t size, bool host) {
  memset(b, 0, sizeof *b);
  VkBufferCreateInfo ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage, 0, 0, NULL };
  VkResult r = vkCreateBuffer(dev, &ci, NULL, &b->buf);
  if (r) return fail("vkCreateBuffer", r), false;
  VkMemoryRequirements rq;
  vkGetBufferMemoryRequirements(dev, b->buf, &rq);
  VkFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  if (!alloc(&rq, host ? hv : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, host ? hv : 0, &b->mem)) {
    vkDestroyBuffer(dev, b->buf, NULL);
    b->buf = 0;
    return false;
  }
  vkBindBufferMemory(dev, b->buf, b->mem, 0);
  b->size = size;
  return true;
}

static void bury(int kind, uint64_t a, uint64_t b, uint64_t c) {
  if (ndead == capdead) {
    int nc = capdead ? capdead * 2 : 64;
    Dead *nd = realloc(dead, sizeof *nd * (size_t)nc);
    if (!nd) abort();
    dead = nd, capdead = nc;
  }
  dead[ndead++] = (Dead){ kind, a, b, c, frame_no };
}

static void destroy(const Dead *d) {
  switch (d->kind) {
  case DEAD_IMAGE:
    vkDestroyImageView(dev, d->b, NULL);
    vkDestroyImage(dev, d->a, NULL);
    vkFreeMemory(dev, d->c, NULL);
    break;
  case DEAD_BUFFER:
    vkDestroyBuffer(dev, d->a, NULL);
    vkFreeMemory(dev, d->c, NULL);
    break;
  case DEAD_PIPELINE: vkDestroyPipeline(dev, d->a, NULL); break;
  case DEAD_SAMPLER: vkDestroySampler(dev, d->a, NULL); break;
  }
}

/* frees what no frame still in flight can use */
static void reap(bool all) {
  int k = 0;
  for (int i = 0; i < ndead; i++)
    if (all || dead[i].frame < done_no) destroy(&dead[i]);
    else dead[k++] = dead[i];
  ndead = k;
}

static void free_buffer_now(GxBuf *b) {
  if (b->buf) vkDestroyBuffer(dev, b->buf, NULL);
  if (b->mem) vkFreeMemory(dev, b->mem, NULL);
  memset(b, 0, sizeof *b);
}

/* ---- one-off commands (outside a frame) --------------------------------------------------------- */
static VkCommandBuffer once_begin(void) {
  VkCommandBuffer c;
  VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, cpool, 0, 1 };
  if (vkAllocateCommandBuffers(dev, &ai, &c)) return NULL;
  VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
  vkBeginCommandBuffer(c, &bi);
  return c;
}
static void once_end(VkCommandBuffer c) {
  vkEndCommandBuffer(c);
  VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &c, 0, NULL };
  vkQueueSubmit(queue, 1, &si, 0);
  vkQueueWaitIdle(queue);
  /* the pool's buffers are freed with the pool; a one-off one is reset and left (they are few) */
}

/* ---- layouts ----------------------------------------------------------------------------------------- */
static void barrier(VkCommandBuffer c, GxTex *t, int layout) {
  if (t->layout == layout) return;
  VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = t->layout;
  b.newLayout = layout;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = t->img;
  b.subresourceRange = (VkImageSubresourceRange){ t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
  t->layout = layout;
}
static int read_layout(const GxTex *t) {
  return t->depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

/* ---- resources ---------------------------------------------------------------------------------------- */
static bool make_image(GxTex *t, int fmt, int w, int h, int samples, unsigned usage) {
  memset(t, 0, sizeof *t);
  t->fmt = fmt, t->w = w, t->h = h, t->samples = samples, t->usage = usage;
  t->depth = fmt == VK_FORMAT_D32_SFLOAT;
  VkImageCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
  ci.imageType = 1; /* 2D */
  ci.format = fmt;
  ci.extent = (VkExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
  ci.mipLevels = ci.arrayLayers = 1;
  ci.samples = (VkFlags)samples;
  ci.tiling = 0; /* optimal */
  ci.usage = (usage & GX_TEX_SAMPLED ? VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0) |
             (usage & GX_TEX_COLOR ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0) |
             (usage & GX_TEX_DEPTH ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0);
  VkResult r = vkCreateImage(dev, &ci, NULL, &t->img);
  if (r) return fail("vkCreateImage", r), false;
  VkMemoryRequirements rq;
  vkGetImageMemoryRequirements(dev, t->img, &rq);
  if (!alloc(&rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &t->mem)) {
    vkDestroyImage(dev, t->img, NULL);
    return false;
  }
  vkBindImageMemory(dev, t->img, t->mem, 0);
  VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
  vi.image = t->img;
  vi.viewType = 1; /* 2D */
  vi.format = fmt;
  vi.subresourceRange = (VkImageSubresourceRange){ t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  r = vkCreateImageView(dev, &vi, NULL, &t->view);
  if (r) {
    vkDestroyImage(dev, t->img, NULL);
    vkFreeMemory(dev, t->mem, NULL);
    return fail("vkCreateImageView", r), false;
  }
  return true;
}

GxTex *gx_texture(GxFormat f, int w, int h, int samples, unsigned usage) {
  GxTex *t = malloc(sizeof *t);
  if (!t || !make_image(t, vk_format(f), w, h, samples < 1 ? 1 : samples, usage)) return free(t), NULL;
  if (usage & GX_TEX_SAMPLED) { /* sampled before it is ever drawn into: give it the read layout now */
    VkCommandBuffer c = in_pass || !cmd ? once_begin() : cmd;
    if (c) barrier(c, t, read_layout(t));
    if (c && c != cmd) once_end(c);
  }
  return t;
}

void gx_texture_free(GxTex *t) {
  if (!t) return;
  if (rb.t == t) rb.t = NULL;
  if (!t->swap) bury(DEAD_IMAGE, t->img, t->view, t->mem);
  free(t);
}

GxBuf *gx_buffer(unsigned usage, uint32_t size) {
  GxBuf *b = malloc(sizeof *b);
  VkFlags u = VK_BUFFER_USAGE_TRANSFER_DST_BIT | (usage & GX_BUF_VERTEX ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : 0) |
              (usage & GX_BUF_INDEX ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : 0);
  if (!b || !make_buffer(b, u, size ? size : 4, false)) return free(b), NULL;
  return b;
}

void gx_buffer_free(GxBuf *b) {
  if (!b) return;
  bury(DEAD_BUFFER, b->buf, 0, b->mem);
  free(b);
}

GxSampler *gx_sampler(bool linear, bool compare) {
  GxSampler *s = malloc(sizeof *s);
  if (!s) return NULL;
  VkSamplerCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
  ci.magFilter = ci.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  ci.mipmapMode = 0;
  ci.addressModeU = ci.addressModeV = ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  ci.maxAnisotropy = 1;
  ci.compareEnable = compare;
  ci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  ci.maxLod = 1000;
  VkResult r = vkCreateSampler(dev, &ci, NULL, &s->s);
  if (r) return fail("vkCreateSampler", r), free(s), NULL;
  return s;
}

void gx_sampler_free(GxSampler *s) {
  if (!s) return;
  bury(DEAD_SAMPLER, s->s, 0, 0);
  free(s);
}

bool gx_supports_samples(GxFormat f, int n) {
  VkImageFormatProperties p;
  VkFlags use = f == GX_D32F ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (vkGetPhysicalDeviceImageFormatProperties(phys, vk_format(f), 1, 0, use, 0, &p)) return n == 1;
  if (!(p.sampleCounts & (VkFlags)n)) return false;
  if (f != GX_D32F) return gx_supports_samples(GX_D32F, n); /* the depth buffer goes with it */
  return true;
}

/* ---- pipelines ------------------------------------------------------------------------------------------- */
static VkShaderModule module(const void *code, size_t size) {
  uint32_t *words = malloc(size + 4); /* the code must be 4-byte aligned */
  if (!words) return 0;
  memcpy(words, code, size);
  VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, size, words };
  VkShaderModule m = 0;
  VkResult r = vkCreateShaderModule(dev, &ci, NULL, &m);
  free(words);
  if (r) fail("vkCreateShaderModule", r);
  return m;
}

static int attr_format(GxAttrFormat f) {
  return f == GX_F32x3 ? VK_FORMAT_R32G32B32_SFLOAT : f == GX_F32x4 ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
}

GxPipe *gx_pipeline(const GxPipeDesc *d) {
  VkShaderModule vs = module(d->vs_code, d->vs_size), fs = module(d->fs_code, d->fs_size);
  GxPipe *p = NULL;
  if (!vs || !fs) goto out;
  VkPipelineShaderStageCreateInfo st[2] = {
    { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL },
    { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL },
  };
  VkVertexInputBindingDescription vb = { 0, d->stride, d->per_instance ? 1 : 0 };
  VkVertexInputAttributeDescription at[4];
  for (int i = 0; i < d->nattrs && i < 4; i++)
    at[i] = (VkVertexInputAttributeDescription){ (uint32_t)d->attrs[i].location, 0, attr_format(d->attrs[i].format), d->attrs[i].offset };
  VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0,
                                              d->stride ? 1u : 0u, &vb, (uint32_t)d->nattrs, at };
  VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
                                                d->prim == GX_LINES ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0 };
  VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL };
  VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
  rs.cullMode = 0;
  rs.frontFace = 0; /* counter-clockwise */
  rs.lineWidth = 1;
  if (d->depth_bias) {
    rs.depthBiasEnable = 1;
    rs.depthBiasConstantFactor = 2;
    rs.depthBiasSlopeFactor = 2.5f;
  }
  VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
  ms.rasterizationSamples = (VkFlags)(d->samples < 1 ? 1 : d->samples);
  VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
  ds.depthTestEnable = d->depth_test;
  ds.depthWriteEnable = d->depth_write;
  ds.depthCompareOp = d->depth_less_equal ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_GREATER_OR_EQUAL;
  ds.maxDepthBounds = 1;
  VkPipelineColorBlendAttachmentState ba = { 0 };
  ba.colorWriteMask = 0xf;
  if (d->blend_premul) {
    ba.blendEnable = 1;
    ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstColorBlendFactor = ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  }
  VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, 0, 0,
                                             d->has_color ? 1u : 0u, &ba, { 0 } };
  int dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn };
  int cfmt = vk_format(d->color_format);
  VkPipelineRenderingCreateInfo ri = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, d->has_color ? 1u : 0u,
                                       &cfmt, d->has_depth ? vk_format(d->depth_format) : 0, 0 };
  VkGraphicsPipelineCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &ri };
  ci.stageCount = 2;
  ci.pStages = st;
  ci.pVertexInputState = &vi;
  ci.pInputAssemblyState = &ia;
  ci.pViewportState = &vp;
  ci.pRasterizationState = &rs;
  ci.pMultisampleState = &ms;
  ci.pDepthStencilState = &ds;
  ci.pColorBlendState = &cb;
  ci.pDynamicState = &dy;
  ci.layout = playout;
  ci.basePipelineIndex = -1;
  p = malloc(sizeof *p);
  if (p) {
    VkResult r = vkCreateGraphicsPipelines(dev, 0, 1, &ci, NULL, &p->p);
    if (r) fail("vkCreateGraphicsPipelines", r), free(p), p = NULL;
  }
out:
  if (vs) vkDestroyShaderModule(dev, vs, NULL);
  if (fs) vkDestroyShaderModule(dev, fs, NULL);
  return p;
}

void gx_pipeline_free(GxPipe *p) {
  if (!p) return;
  bury(DEAD_PIPELINE, p->p, 0, 0);
  free(p);
}

/* ---- the device ------------------------------------------------------------------------------------- */
static bool has_ext(const VkExtensionProperties *e, uint32_t n, const char *name) {
  for (uint32_t i = 0; i < n; i++)
    if (!strcmp(e[i].extensionName, name)) return true;
  return false;
}

static bool load_instance(void) {
#define X(n, r, a) n = (r(VKAPI *) a)gipa(inst, #n);
  VK_INSTANCE_CALLS(X)
#undef X
  vkCreateWaylandSurfaceKHR = (void *)gipa(inst, "vkCreateWaylandSurfaceKHR");
  vkCreateXlibSurfaceKHR = (void *)gipa(inst, "vkCreateXlibSurfaceKHR");
  vkCreateWin32SurfaceKHR = (void *)gipa(inst, "vkCreateWin32SurfaceKHR");
  return vkEnumeratePhysicalDevices && vkCreateDevice && vkGetDeviceProcAddr;
}

static bool load_device(void) {
  bool ok = true;
#define X(n, r, a) n = (r(VKAPI *) a)vkGetDeviceProcAddr(dev, #n), ok = ok && n;
  VK_DEVICE_CALLS(X)
#undef X
  if (!vkCmdBeginRendering) vkCmdBeginRendering = (void *)vkGetDeviceProcAddr(dev, "vkCmdBeginRenderingKHR");
  if (!vkCmdEndRendering) vkCmdEndRendering = (void *)vkGetDeviceProcAddr(dev, "vkCmdEndRenderingKHR");
  return ok || (vkCmdBeginRendering && vkCmdEndRendering);
}

static void destroy_chain(void) {
  for (uint32_t i = 0; i < nchain; i++) {
    if (chain_tex[i].view) vkDestroyImageView(dev, chain_tex[i].view, NULL);
    if (chain_done[i]) vkDestroySemaphore(dev, chain_done[i], NULL);
    chain_tex[i].view = 0;
    chain_done[i] = 0;
  }
  nchain = 0;
}

static bool make_chain(void) {
  vkDeviceWaitIdle(dev);
  VkSurfaceCapabilitiesKHR caps;
  VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
  if (r) return fail("surface capabilities", r), false;
  int w = 0, h = 0;
  plat_window_pixels(&w, &h);
  VkExtent2D ext = caps.currentExtent.width != 0xFFFFFFFFu ? caps.currentExtent : (VkExtent2D){ (uint32_t)w, (uint32_t)h };
  if (ext.width < caps.minImageExtent.width) ext.width = caps.minImageExtent.width;
  if (ext.height < caps.minImageExtent.height) ext.height = caps.minImageExtent.height;
  if (caps.maxImageExtent.width && ext.width > caps.maxImageExtent.width) ext.width = caps.maxImageExtent.width;
  if (caps.maxImageExtent.height && ext.height > caps.maxImageExtent.height) ext.height = caps.maxImageExtent.height;
  if (!ext.width || !ext.height) return false; /* minimised */
  VkSurfaceFormatKHR fm[64];
  uint32_t nf = 64;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nf, fm);
  chain_fmt = 0;
  for (uint32_t i = 0; i < nf && !chain_fmt; i++)
    if (fm[i].format == VK_FORMAT_B8G8R8A8_UNORM && fm[i].colorSpace == 0) chain_fmt = fm[i].format;
  for (uint32_t i = 0; i < nf && !chain_fmt; i++)
    if (fm[i].format == VK_FORMAT_R8G8B8A8_UNORM && fm[i].colorSpace == 0) chain_fmt = fm[i].format;
  if (!chain_fmt) return fail("no 8-bit surface format", 0), false;
  int modes[16];
  uint32_t nm = 16;
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &nm, modes);
  has_mailbox = false;
  for (uint32_t i = 0; i < nm; i++) has_mailbox |= modes[i] == VK_PRESENT_MODE_MAILBOX_KHR;
  uint32_t count = caps.minImageCount + 1;
  if (count < 3 && has_mailbox) count = 3;
  if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
  VkSwapchainCreateInfoKHR ci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
  ci.surface = surface;
  ci.minImageCount = count;
  ci.imageFormat = chain_fmt;
  ci.imageColorSpace = 0; /* sRGB non-linear */
  ci.imageExtent = ext;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ci.preTransform = caps.currentTransform;
  ci.compositeAlpha = caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                      : caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
                                                                                          : caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha;
  ci.presentMode = has_mailbox && !want_vsync ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
  ci.clipped = 1;
  ci.oldSwapchain = chain;
  VkSwapchainKHR nc;
  r = vkCreateSwapchainKHR(dev, &ci, NULL, &nc);
  destroy_chain();
  if (chain) vkDestroySwapchainKHR(dev, chain, NULL);
  chain = 0;
  if (r) return fail("vkCreateSwapchainKHR", r), false;
  chain = nc;
  nchain = MAXSWAP;
  vkGetSwapchainImagesKHR(dev, chain, &nchain, chain_img);
  for (uint32_t i = 0; i < nchain; i++) {
    GxTex *t = &chain_tex[i];
    memset(t, 0, sizeof *t);
    t->img = chain_img[i];
    t->fmt = chain_fmt, t->w = (int)ext.width, t->h = (int)ext.height, t->samples = 1, t->swap = true;
    t->usage = GX_TEX_COLOR;
    VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = t->img;
    vi.viewType = 1;
    vi.format = chain_fmt;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(dev, &vi, NULL, &t->view);
    VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vkCreateSemaphore(dev, &si, NULL, &chain_done[i]);
  }
  chain_w = (int)ext.width, chain_h = (int)ext.height;
  chain_bad = false;
  return true;
}

static bool make_frames(void) {
  VkCommandPoolCreateInfo pc = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfam };
  if (vkCreateCommandPool(dev, &pc, NULL, &cpool)) return fail("vkCreateCommandPool", 0), false;
  VkDescriptorSetLayoutBinding ub[2] = { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL },
                                         { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL } };
  VkDescriptorSetLayoutBinding tb[4];
  for (int i = 0; i < 4; i++) tb[i] = (VkDescriptorSetLayoutBinding){ (uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
  VkDescriptorSetLayoutCreateInfo lc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL };
  vkCreateDescriptorSetLayout(dev, &lc, NULL, &lay_empty);
  lc.bindingCount = 2, lc.pBindings = ub;
  vkCreateDescriptorSetLayout(dev, &lc, NULL, &lay_ubo_vs);
  ub[0].stageFlags = ub[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  vkCreateDescriptorSetLayout(dev, &lc, NULL, &lay_ubo_fs);
  lc.bindingCount = 4, lc.pBindings = tb;
  vkCreateDescriptorSetLayout(dev, &lc, NULL, &lay_tex);
  VkDescriptorSetLayout sets[4] = { lay_empty, lay_ubo_vs, lay_tex, lay_ubo_fs };
  VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 4, sets, 0, NULL };
  if (vkCreatePipelineLayout(dev, &pl, NULL, &playout)) return fail("vkCreatePipelineLayout", 0), false;
  for (int f = 0; f < FRAMES; f++) {
    Frame *F = &frames[f];
    VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, cpool, 0, 1 };
    VkFenceCreateInfo fc = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };
    VkSemaphoreCreateInfo sc = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (vkAllocateCommandBuffers(dev, &ai, &F->cmd) || vkCreateFence(dev, &fc, NULL, &F->fence) ||
        vkCreateSemaphore(dev, &sc, NULL, &F->acquired))
      return fail("frame objects", 0), false;
    if (!make_buffer(&F->ring, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, RING, true)) return false;
    vkMapMemory(dev, F->ring.mem, 0, VK_WHOLE_SIZE, 0, (void **)&F->ring_map);
    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 8 } };
    VkDescriptorPoolCreateInfo pci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1024, 2, ps };
    if (vkCreateDescriptorPool(dev, &pci, NULL, &F->pool)) return fail("vkCreateDescriptorPool", 0), false;
  }
  return true;
}

static VkDescriptorSet uset_fs[FRAMES]; /* each frame's fragment uniform set, beside Frame.uset */

/* the ring's two uniform sets, made after each reset of the pool */
static void ring_sets(Frame *F) {
  VkDescriptorSetLayout l[2] = { lay_ubo_vs, lay_ubo_fs };
  VkDescriptorSet s[2];
  VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, F->pool, 2, l };
  vkAllocateDescriptorSets(dev, &ai, s);
  VkDescriptorBufferInfo bi = { F->ring.buf, 0, 512 };
  VkWriteDescriptorSet w[4];
  for (int i = 0; i < 4; i++)
    w[i] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, s[i / 2], (uint32_t)(i & 1), 0, 1,
                                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, NULL, &bi, NULL };
  vkUpdateDescriptorSets(dev, 4, w, 0, NULL);
  F->uset = s[0];
  uset_fs[F - frames] = s[1];
}

bool gx_open(const void *native_window, int w, int h, bool vsync) {
  err[0] = 0;
  want_vsync = vsync;
  native = native_window;
  if (!lib) lib = LIB_OPEN();
  if (!lib) return fail("no Vulkan loader", 0), false;
  gipa = (PFN_vkGetInstanceProcAddr)LIB_SYM(lib, "vkGetInstanceProcAddr");
  if (!gipa) return fail("no vkGetInstanceProcAddr", 0), gx_close(), false;
#define X(n, r, a) n = (r(VKAPI *) a)gipa(NULL, #n);
  VK_GLOBAL_CALLS(X)
#undef X
  if (!vkCreateInstance) return fail("no vkCreateInstance", 0), gx_close(), false;
  /* one instance for the life of the program, with every window system's surface the loader has: a device opened
   * and closed again (the tests, a new renderer) keeps the driver loaded */
  static bool has_wl, has_x11, has_win;
  VkResult r;
  if (!inst) {
    VkExtensionProperties ie[64];
    uint32_t nie = 64;
    vkEnumerateInstanceExtensionProperties(NULL, &nie, ie);
    const char *exts[4];
    uint32_t nexts = 0;
    if (has_ext(ie, nie, "VK_KHR_surface")) {
      exts[nexts++] = "VK_KHR_surface";
      if ((has_wl = has_ext(ie, nie, "VK_KHR_wayland_surface"))) exts[nexts++] = "VK_KHR_wayland_surface";
      if ((has_x11 = has_ext(ie, nie, "VK_KHR_xlib_surface"))) exts[nexts++] = "VK_KHR_xlib_surface";
      if ((has_win = has_ext(ie, nie, "VK_KHR_win32_surface"))) exts[nexts++] = "VK_KHR_win32_surface";
    }
    /* A4C_VK_VALIDATE=1: the Khronos validation layer, when the system has it (tests only) */
    const char *layer = "VK_LAYER_KHRONOS_validation";
    uint32_t nlayers = 0;
    if (getenv("A4C_VK_VALIDATE") && vkEnumerateInstanceLayerProperties) {
      VkLayerProperties lp[64];
      uint32_t nl = 64;
      vkEnumerateInstanceLayerProperties(&nl, lp);
      for (uint32_t i = 0; i < nl; i++)
        if (!strcmp(lp[i].layerName, layer)) nlayers = 1;
      plat_log("gpu: validation layer %s", nlayers ? "on" : "not installed");
    }
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "Tailon Lock", 1, "Tailon Lock", 1, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, nlayers, &layer, nexts, exts };
    r = vkCreateInstance(&ici, NULL, &inst);
    if (r) return fail("vkCreateInstance", r), inst = NULL, gx_close(), false;
    if (!load_instance()) return fail("instance calls", 0), gx_close(), false;
  }
  if (native && !((native->kind == PNAT_WAYLAND && has_wl) || (native->kind == PNAT_X11 && has_x11) ||
                  (native->kind == PNAT_WIN32 && has_win)))
    return fail("no Vulkan surface for this window", 0), gx_close(), false;
  if (native) {
    if (native->kind == PNAT_WAYLAND && vkCreateWaylandSurfaceKHR) {
      VkWaylandSurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, NULL, 0, native->display, native->window };
      r = vkCreateWaylandSurfaceKHR(inst, &si, NULL, &surface);
    } else if (native->kind == PNAT_X11 && vkCreateXlibSurfaceKHR) {
      VkXlibSurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, NULL, 0, native->display, (unsigned long)(uintptr_t)native->window };
      r = vkCreateXlibSurfaceKHR(inst, &si, NULL, &surface);
    } else if (native->kind == PNAT_WIN32 && vkCreateWin32SurfaceKHR) {
      VkWin32SurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, NULL, 0, native->display, native->window };
      r = vkCreateWin32SurfaceKHR(inst, &si, NULL, &surface);
    } else {
      r = -1;
    }
    if (r) return fail("the window's Vulkan surface", r), gx_close(), false;
  }
  /* a discrete GPU first, then an integrated one, then anything (a software one last) */
  VkPhysicalDevice pd[16];
  uint32_t npd = 16;
  vkEnumeratePhysicalDevices(inst, &npd, pd);
  int best = -1, best_score = -1;
  static uint8_t propbuf[2048];
  for (uint32_t i = 0; i < npd; i++) {
    VkPhysicalDevicePropertiesHead *p = (void *)propbuf;
    vkGetPhysicalDeviceProperties(pd[i], propbuf);
    if (p->apiVersion < VK_API_VERSION_1_3) continue;
    uint32_t nq = 16;
    VkQueueFamilyProperties qp[16];
    vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &nq, qp);
    int q = -1;
    for (uint32_t k = 0; k < nq && q < 0; k++) {
      VkBool32 pres = 1;
      if (surface) vkGetPhysicalDeviceSurfaceSupportKHR(pd[i], k, surface, &pres);
      if ((qp[k].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) q = (int)k;
    }
    if (q < 0) continue;
    int score = p->deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p->deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
    if (score > best_score) best = (int)i, best_score = score, qfam = (uint32_t)q;
  }
  if (best < 0) return fail("no Vulkan 1.3 GPU", 0), gx_close(), false;
  phys = pd[best];
  vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
  float prio = 1;
  VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, qfam, 1, &prio };
  VkPhysicalDeviceDynamicRenderingFeatures drf = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, NULL, 1 };
  const char *dext[1] = { "VK_KHR_swapchain" };
  VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &drf, 0, 1, &qi, 0, NULL, surface ? 1u : 0u, dext, NULL };
  r = vkCreateDevice(phys, &dci, NULL, &dev);
  if (r) return fail("vkCreateDevice", r), gx_close(), false;
  if (!load_device()) return fail("device calls", 0), gx_close(), false;
  vkGetDeviceQueue(dev, qfam, 0, &queue);
  if (!make_frames()) return gx_close(), false;
  if (surface) {
    if (!make_chain() && err[0]) return gx_close(), false;
  } else {
    chain_fmt = VK_FORMAT_B8G8R8A8_UNORM;
    offscreen = gx_texture(GX_BGRA8, w > 0 ? w : 1280, h > 0 ? h : 720, 1, GX_TEX_COLOR | GX_TEX_SAMPLED);
    if (!offscreen) return gx_close(), false;
  }
  return true;
}

void gx_close(void) {
  if (dev) {
    vkDeviceWaitIdle(dev);
    if (offscreen) gx_texture_free(offscreen);
    offscreen = NULL;
    free_buffer_now(&rb.buf);
    reap(true);
    destroy_chain();
    if (chain) vkDestroySwapchainKHR(dev, chain, NULL);
    chain = 0;
    for (int f = 0; f < FRAMES; f++) {
      Frame *F = &frames[f];
      if (F->ring_map) vkUnmapMemory(dev, F->ring.mem);
      if (F->stage_map) vkUnmapMemory(dev, F->stage.mem);
      free_buffer_now(&F->ring);
      free_buffer_now(&F->stage);
      if (F->pool) vkDestroyDescriptorPool(dev, F->pool, NULL);
      if (F->fence) vkDestroyFence(dev, F->fence, NULL);
      if (F->acquired) vkDestroySemaphore(dev, F->acquired, NULL);
      memset(F, 0, sizeof *F);
    }
    if (playout) vkDestroyPipelineLayout(dev, playout, NULL);
    VkDescriptorSetLayout *ls[4] = { &lay_empty, &lay_ubo_vs, &lay_tex, &lay_ubo_fs };
    for (int i = 0; i < 4; i++)
      if (*ls[i]) vkDestroyDescriptorSetLayout(dev, *ls[i], NULL), *ls[i] = 0;
    playout = 0;
    if (cpool) vkDestroyCommandPool(dev, cpool, NULL);
    cpool = 0;
    vkDestroyDevice(dev, NULL);
  }
  dev = NULL;
  if (surface && vkDestroySurfaceKHR) vkDestroySurfaceKHR(inst, surface, NULL);
  surface = 0;
  free(dead);
  dead = NULL;
  ndead = capdead = 0;
  /* the loader stays open: the driver it loaded keeps state of its own, and opening it again is free */
  cmd = NULL;
  in_pass = false;
  memset(&rb, 0, sizeof rb);
  frame_no = 1, done_no = 0, fi = 0;
}

/* ---- the frame ----------------------------------------------------------------------------------------- */
static GxTex *frame_swap;

bool gx_frame_begin(GxTex **swap, int *w, int *h) {
  Frame *F = &frames[fi];
  vkWaitForFences(dev, 1, &F->fence, 1, ~0ull);
  if (F->number + 1 > done_no) done_no = F->number + 1; /* that frame and all before it are done */
  reap(false);
  if (surface) {
    int ww = 0, wh = 0;
    plat_window_pixels(&ww, &wh);
    if (ww <= 0 || wh <= 0) return false;
    if (!chain || chain_bad || ww != chain_w || wh != chain_h) {
      if (!make_chain()) return false;
    }
    VkResult r = vkAcquireNextImageKHR(dev, chain, ~0ull, F->acquired, 0, &chain_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
      chain_bad = true;
      return false;
    }
    if (r && r != VK_SUBOPTIMAL_KHR) return false;
    if (r == VK_SUBOPTIMAL_KHR) chain_bad = true;
    frame_swap = &chain_tex[chain_index];
    frame_swap->layout = VK_IMAGE_LAYOUT_UNDEFINED; /* its old contents are not kept */
  } else {
    frame_swap = offscreen;
  }
  vkResetFences(dev, 1, &F->fence);
  vkResetCommandBuffer(F->cmd, 0);
  vkResetDescriptorPool(dev, F->pool, 0);
  ring_sets(F);
  F->ring_used = 0;
  F->stage_used = 0;
  F->number = frame_no;
  cmd = F->cmd;
  VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
  vkBeginCommandBuffer(cmd, &bi);
  memset(uoff, 0, sizeof uoff);
  udirty = true;
  *swap = frame_swap;
  *w = frame_swap->w;
  *h = frame_swap->h;
  return true;
}

/* room in the frame's staging buffer; a bigger one replaces it when it is full */
static uint8_t *stage(uint32_t size, VkBuffer *buf, uint32_t *off) {
  Frame *F = &frames[fi];
  uint32_t at = (F->stage_used + 255) & ~255u;
  if (!F->stage.buf || at + size > F->stage.size) {
    uint32_t cap = F->stage.size ? F->stage.size : 1u << 20;
    while (cap < size) cap *= 2;
    if (F->stage.buf) {
      vkUnmapMemory(dev, F->stage.mem);
      bury(DEAD_BUFFER, F->stage.buf, 0, F->stage.mem);
    }
    if (!make_buffer(&F->stage, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, cap * 2, true)) return NULL;
    vkMapMemory(dev, F->stage.mem, 0, VK_WHOLE_SIZE, 0, (void **)&F->stage_map);
    at = 0;
  }
  F->stage_used = at + size;
  *buf = F->stage.buf;
  *off = at;
  return F->stage_map + at;
}

void gx_upload_buffer(GxBuf *b, const void *data, uint32_t size) {
  if (!b || !size || !cmd) return;
  VkBuffer sb;
  uint32_t off;
  uint8_t *p = stage(size, &sb, &off);
  if (!p) return;
  memcpy(p, data, size);
  VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
  VkBufferCopy c = { off, 0, size };
  vkCmdCopyBuffer(cmd, sb, b->buf, 1, &c);
  VkMemoryBarrier after = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT };
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
}

void gx_upload_texture(GxTex *t, const void *px, int w, int h) {
  if (!t || !cmd) return;
  uint32_t size = (uint32_t)w * (uint32_t)h * 4;
  VkBuffer sb;
  uint32_t off;
  uint8_t *p = stage(size, &sb, &off);
  if (!p) return;
  memcpy(p, px, size);
  barrier(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  VkBufferImageCopy c = { off, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)w, (uint32_t)h, 1 } };
  vkCmdCopyBufferToImage(cmd, sb, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
  barrier(cmd, t, read_layout(t));
}

static GxTex *pass_tex[3];
void gx_viewport(float x, float y, float w, float h);

void gx_pass_begin(const GxPass *p) {
  static const int LOAD[3] = { VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_LOAD };
  VkRenderingAttachmentInfo ca = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO }, da = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
  VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
  GxTex *size_of = p->color ? p->color : p->depth;
  if (!size_of) return; /* a pass draws into something */
  ri.renderArea.extent = (VkExtent2D){ (uint32_t)size_of->w, (uint32_t)size_of->h };
  ri.layerCount = 1;
  pass_tex[0] = p->color, pass_tex[1] = p->resolve, pass_tex[2] = p->depth;
  if (p->color) {
    barrier(cmd, p->color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ca.imageView = p->color->view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = LOAD[p->color_load];
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE; /* a multisampled one too: the next view's pass loads it */
    memcpy(ca.clearValue.color.float32, p->clear, sizeof p->clear);
    if (p->resolve) {
      barrier(cmd, p->resolve, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      ca.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
      ca.resolveImageView = p->resolve->view;
      ca.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
  }
  if (p->depth) {
    barrier(cmd, p->depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    da.imageView = p->depth->view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = LOAD[p->depth_load];
    da.storeOp = p->depth_store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    da.clearValue.depthStencil.depth = p->clear_depth;
    ri.pDepthAttachment = &da;
  }
  vkCmdBeginRendering(cmd, &ri);
  in_pass = true;
  udirty = true;
  gx_viewport(0, 0, (float)size_of->w, (float)size_of->h);
  VkRect2D sc = { { 0, 0 }, ri.renderArea.extent };
  vkCmdSetScissor(cmd, 0, 1, &sc);
}

void gx_pass_end(void) {
  vkCmdEndRendering(cmd);
  in_pass = false;
  /* what this pass drew and will be sampled later moves to the read layout now */
  for (int i = 0; i < 3; i++)
    if (pass_tex[i] && (pass_tex[i]->usage & GX_TEX_SAMPLED)) barrier(cmd, pass_tex[i], read_layout(pass_tex[i]));
}

void gx_bind_pipeline(GxPipe *p) {
  vkCmdBindPipeline(cmd, 0, p->p);
  udirty = true;
}

/* flipped (a negative height, core since Vulkan 1.1), so clip space has y up as gx.h says */
void gx_viewport(float x, float y, float w, float h) {
  VkViewport vp = { x, y + h, w, -h, 0, 1 };
  vkCmdSetViewport(cmd, 0, 1, &vp);
}

void gx_scissor(int x, int y, int w, int h) {
  VkRect2D r = { { x < 0 ? 0 : x, y < 0 ? 0 : y }, { (uint32_t)(w < 0 ? 0 : w), (uint32_t)(h < 0 ? 0 : h) } };
  vkCmdSetScissor(cmd, 0, 1, &r);
}

void gx_bind_vertex(GxBuf *b) {
  VkDeviceSize off = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &b->buf, &off);
}

void gx_bind_index32(GxBuf *b) { vkCmdBindIndexBuffer(cmd, b->buf, 0, 1 /* uint32 */); }

void gx_bind_textures(GxTex *const *t, GxSampler *const *s, int n) {
  if (n < 1) return;
  Frame *F = &frames[fi];
  VkDescriptorSet set;
  VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, F->pool, 1, &lay_tex };
  if (vkAllocateDescriptorSets(dev, &ai, &set)) return;
  VkDescriptorImageInfo ii[4];
  VkWriteDescriptorSet w[4];
  for (int i = 0; i < 4; i++) {
    int k = i < n ? i : 0; /* unused slots repeat the first */
    ii[i] = (VkDescriptorImageInfo){ s[k]->s, t[k]->view, read_layout(t[k]) };
    w[i] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, (uint32_t)i, 0, 1,
                                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii[i], NULL, NULL };
  }
  vkUpdateDescriptorSets(dev, 4, w, 0, NULL);
  vkCmdBindDescriptorSets(cmd, 0, playout, 2, 1, &set, 0, NULL);
}

void gx_uniform(int stage_, int slot, const void *data, uint32_t size) {
  Frame *F = &frames[fi];
  if (F->ring_used + 512 > RING || size > 512) return;
  memcpy(F->ring_map + F->ring_used, data, size);
  uoff[stage_ & 1][slot & 1] = F->ring_used;
  F->ring_used += (size + UALIGN - 1) & ~(UALIGN - 1);
  if (F->ring_used < uoff[stage_ & 1][slot & 1] + 512) F->ring_used = uoff[stage_ & 1][slot & 1] + 512; /* the bound range */
  F->ring_used = (F->ring_used + UALIGN - 1) & ~(UALIGN - 1);
  udirty = true;
}

static void flush_uniforms(void) {
  if (!udirty) return;
  Frame *F = &frames[fi];
  VkDescriptorSet sets[1] = { F->uset };
  vkCmdBindDescriptorSets(cmd, 0, playout, 1, 1, sets, 2, uoff[0]);
  sets[0] = uset_fs[fi];
  vkCmdBindDescriptorSets(cmd, 0, playout, 3, 1, sets, 2, uoff[1]);
  udirty = false;
}

void gx_draw(uint32_t verts, uint32_t instances, uint32_t first_vert, uint32_t first_instance) {
  flush_uniforms();
  vkCmdDraw(cmd, verts, instances, first_vert, first_instance);
}

void gx_draw_indexed(uint32_t indices) {
  flush_uniforms();
  vkCmdDrawIndexed(cmd, indices, 1, 0, 0, 0);
}

void gx_readback(GxTex *t, void *out) {
  rb.t = t;
  rb.out = out;
}

void gx_frame_end(void) {
  Frame *F = &frames[fi];
  bool reading = rb.t && rb.out;
  if (reading) {
    uint32_t size = (uint32_t)rb.t->w * (uint32_t)rb.t->h * 4;
    if (rb.buf.size < size) {
      if (rb.buf.buf) vkUnmapMemory(dev, rb.buf.mem), free_buffer_now(&rb.buf);
      if (!make_buffer(&rb.buf, VK_BUFFER_USAGE_TRANSFER_DST_BIT, size, true)) reading = false;
    }
    if (reading) {
      barrier(cmd, rb.t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      VkBufferImageCopy c = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)rb.t->w, (uint32_t)rb.t->h, 1 } };
      vkCmdCopyImageToBuffer(cmd, rb.t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buf.buf, 1, &c);
      VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    }
  }
  if (surface) barrier(cmd, frame_swap, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  else barrier(cmd, frame_swap, read_layout(frame_swap));
  vkEndCommandBuffer(cmd);
  VkFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd, 0, NULL };
  if (surface) {
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &F->acquired;
    si.pWaitDstStageMask = &wait_stage;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &chain_done[chain_index];
  }
  vkQueueSubmit(queue, 1, &si, F->fence);
  if (surface) {
    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &chain_done[chain_index], 1, &chain, &chain_index, NULL };
    VkResult r = vkQueuePresentKHR(queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) chain_bad = true;
  }
  if (reading) {
    vkWaitForFences(dev, 1, &F->fence, 1, ~0ull);
    void *m = NULL;
    if (!vkMapMemory(dev, rb.buf.mem, 0, VK_WHOLE_SIZE, 0, &m) && m) {
      memcpy(rb.out, m, (size_t)rb.t->w * (size_t)rb.t->h * 4);
      vkUnmapMemory(dev, rb.buf.mem);
    }
  }
  rb.t = NULL;
  rb.out = NULL;
  cmd = NULL;
  frame_no++;
  fi = (fi + 1) % FRAMES;
}
