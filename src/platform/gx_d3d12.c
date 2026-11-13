/* gx_d3d12.c - the GPU device (gx.h) on Direct3D 12, through the system's
 * d3d12.dll and dxgi.dll. Original code for this project, written from
 * Microsoft's Direct3D 12 and DXGI documentation.
 *
 * Two frames in flight, each with its command allocator, uniform ring (root
 * constant buffer views), shader-visible descriptor heaps and upload
 * staging. Every texture knows its state and is moved with a barrier when its
 * use changes; a multisampled target is resolved when its pass ends.
 * TL_D3D_DEBUG=1 turns on the debug layer (when Windows' Graphics Tools are
 * installed) and logs what it says. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/gx.h"
#include "platform/plat.h"
#include "platform/plat_core.h"

#define FRAMES 2
#define RING (4u << 20)
#define VIEWS 4096 /* shader-visible texture descriptors per frame */
#define SAMPS 1024 /* shader-visible sampler descriptors per frame */
#define CPU_SRV 1024
#define CPU_RTV 256
#define CPU_DSV 64
#define CPU_SMP 32

struct GxTex {
  ID3D12Resource *res;
  DXGI_FORMAT fmt;
  D3D12_RESOURCE_STATES state;
  int w, h, samples;
  unsigned usage;
  int srv, rtv, dsv; /* slots in the CPU heaps, -1 for none */
  bool swap;
};
struct GxBuf { ID3D12Resource *res; uint32_t size; };
struct GxPipe { ID3D12PipelineState *ps; D3D_PRIMITIVE_TOPOLOGY topo; uint32_t stride; };
struct GxSampler { int slot; };

typedef struct {
  ID3D12CommandAllocator *alloc;
  ID3D12Resource *ring;
  uint8_t *ring_map;
  uint32_t ring_used;
  ID3D12DescriptorHeap *views, *samps;
  int nviews, nsamps;
  ID3D12Resource *stage;
  uint8_t *stage_map;
  uint32_t stage_size, stage_used;
  uint64_t fence_value;
  IUnknown *dead[256]; /* freed while this frame could use them */
  int ndead;
} Frame;

static HMODULE lib_d3d, lib_dxgi;
static IDXGIFactory4 *factory;
static ID3D12Device *dev;
static ID3D12CommandQueue *queue;
static ID3D12GraphicsCommandList *cl;
static ID3D12Fence *fence;
static HANDLE fence_event;
static uint64_t fence_next = 1;
static ID3D12RootSignature *root;
static IDXGISwapChain3 *chain;
static GxTex chain_tex[3];
static int chain_w, chain_h;
static bool vsync;
static HWND hwnd;
static GxTex *offscreen;
static Frame frames[FRAMES];
static int fi;
static bool recording, debug_on;
static ID3D12InfoQueue *info;
static char err[256];
static char drvname[96] = "direct3d12";

static ID3D12DescriptorHeap *h_srv, *h_rtv, *h_dsv, *h_smp;
static bool used_srv[CPU_SRV], used_rtv[CPU_RTV], used_dsv[CPU_DSV], used_smp[CPU_SMP];
static UINT inc_view, inc_rtv, inc_dsv, inc_smp;

static D3D12_GPU_VIRTUAL_ADDRESS ucb[2][2]; /* stage, slot -> the ring address bound */
static struct { GxTex *t; void *out; ID3D12Resource *buf; uint32_t size, pitch; } rb;
static GxTex *pass_color, *pass_resolve, *pass_depth;
static GxPipe *cur_pipe;

static void fail(const char *what, HRESULT hr) { snprintf(err, sizeof err, "%s failed (0x%08lx)", what, (unsigned long)hr); }
const char *gx_error(void) { return err; }
const char *gx_driver(void) { return drvname; }
GxShaderKind gx_shader_kind(void) { return GX_DXIL; }
GxFormat gx_swap_format(void) { return GX_BGRA8; }
bool gx_mailbox(void) { return chain && !vsync; }

static DXGI_FORMAT dx_format(GxFormat f) {
  switch (f) {
  case GX_RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
  case GX_D32F: return DXGI_FORMAT_D32_FLOAT;
  case GX_RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
  default: return DXGI_FORMAT_B8G8R8A8_UNORM;
  }
}

/* ---- descriptor slots ------------------------------------------------------------------------ */
static int take(bool *used, int n) {
  for (int i = 0; i < n; i++)
    if (!used[i]) return used[i] = true, i;
  return -1;
}
static D3D12_CPU_DESCRIPTOR_HANDLE cpu_at(ID3D12DescriptorHeap *h, UINT inc, int i) {
  D3D12_CPU_DESCRIPTOR_HANDLE c = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(h);
  c.ptr += (SIZE_T)inc * (SIZE_T)i;
  return c;
}
static D3D12_GPU_DESCRIPTOR_HANDLE gpu_at(ID3D12DescriptorHeap *h, UINT inc, int i) {
  D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(h);
  g.ptr += (UINT64)inc * (UINT64)i;
  return g;
}
static ID3D12DescriptorHeap *heap(D3D12_DESCRIPTOR_HEAP_TYPE t, UINT n, bool visible) {
  D3D12_DESCRIPTOR_HEAP_DESC d = { t, n, visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
  ID3D12DescriptorHeap *h = NULL;
  HRESULT hr = ID3D12Device_CreateDescriptorHeap(dev, &d, &IID_ID3D12DescriptorHeap, (void **)&h);
  if (FAILED(hr)) fail("CreateDescriptorHeap", hr);
  return h;
}

/* ---- the debug layer's messages ------------------------------------------------------------- */
static void debug_messages(void) {
  if (!info) return;
  UINT64 n = ID3D12InfoQueue_GetNumStoredMessages(info);
  for (UINT64 i = 0; i < n && i < 32; i++) {
    SIZE_T len = 0;
    ID3D12InfoQueue_GetMessage(info, i, NULL, &len);
    D3D12_MESSAGE *m = malloc(len);
    if (m && SUCCEEDED(ID3D12InfoQueue_GetMessage(info, i, m, &len)) && m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
      plat_log("d3d12 debug: %s", m->pDescription);
    free(m);
  }
  ID3D12InfoQueue_ClearStoredMessages(info);
}

/* ---- fences and freeing ------------------------------------------------------------------------ */
static void wait_value(uint64_t v) {
  if (ID3D12Fence_GetCompletedValue(fence) >= v) return;
  ID3D12Fence_SetEventOnCompletion(fence, v, fence_event);
  WaitForSingleObject(fence_event, INFINITE);
}

void gx_wait_idle(void) {
  if (!queue) return;
  uint64_t v = fence_next++;
  ID3D12CommandQueue_Signal(queue, fence, v);
  wait_value(v);
}

static void reap(Frame *F) {
  for (int i = 0; i < F->ndead; i++) IUnknown_Release(F->dead[i]);
  F->ndead = 0;
}

static void bury(IUnknown *u) {
  if (!u) return;
  /* while a frame is recorded it may use u: filed under that frame. Between frames fi already names the next
   * one, whose fence is older than the work just submitted: filed under the frame submitted last, whose fence
   * covers everything that could still use u */
  Frame *F = &frames[recording ? fi : (fi + FRAMES - 1) % FRAMES];
  if (!recording && !F->fence_value) { /* nothing in flight can hold it */
    IUnknown_Release(u);
    return;
  }
  if (F->ndead == (int)(sizeof F->dead / sizeof F->dead[0])) {
    gx_wait_idle();
    for (int f = 0; f < FRAMES; f++) reap(&frames[f]);
  }
  F->dead[F->ndead++] = u;
}

/* ---- resources -------------------------------------------------------------------------------- */
static ID3D12Resource *committed(D3D12_HEAP_TYPE type, const D3D12_RESOURCE_DESC *d, D3D12_RESOURCE_STATES st) {
  D3D12_HEAP_PROPERTIES hp = { type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
  ID3D12Resource *r = NULL;
  HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, d, st, NULL, &IID_ID3D12Resource, (void **)&r);
  if (FAILED(hr)) fail("CreateCommittedResource", hr);
  return r;
}

static ID3D12Resource *buffer_res(D3D12_HEAP_TYPE type, uint32_t size, D3D12_RESOURCE_STATES st) {
  D3D12_RESOURCE_DESC d = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 },
                            D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
  return committed(type, &d, st);
}

static void views_for(GxTex *t) {
  t->srv = t->rtv = t->dsv = -1;
  if (t->usage & GX_TEX_SAMPLED) {
    t->srv = take(used_srv, CPU_SRV);
    D3D12_SHADER_RESOURCE_VIEW_DESC v;
    memset(&v, 0, sizeof v);
    v.Format = t->fmt == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : t->fmt;
    v.ViewDimension = t->samples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    v.Texture2D.MipLevels = 1;
    if (t->srv >= 0) ID3D12Device_CreateShaderResourceView(dev, t->res, &v, cpu_at(h_srv, inc_view, t->srv));
  }
  if (t->usage & GX_TEX_COLOR) {
    t->rtv = take(used_rtv, CPU_RTV);
    if (t->rtv >= 0) ID3D12Device_CreateRenderTargetView(dev, t->res, NULL, cpu_at(h_rtv, inc_rtv, t->rtv));
  }
  if (t->usage & GX_TEX_DEPTH) {
    t->dsv = take(used_dsv, CPU_DSV);
    D3D12_DEPTH_STENCIL_VIEW_DESC v;
    memset(&v, 0, sizeof v);
    v.Format = DXGI_FORMAT_D32_FLOAT;
    v.ViewDimension = t->samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    if (t->dsv >= 0) ID3D12Device_CreateDepthStencilView(dev, t->res, &v, cpu_at(h_dsv, inc_dsv, t->dsv));
  }
}

static void free_views(GxTex *t) {
  if (t->srv >= 0) used_srv[t->srv] = false;
  if (t->rtv >= 0) used_rtv[t->rtv] = false;
  if (t->dsv >= 0) used_dsv[t->dsv] = false;
  t->srv = t->rtv = t->dsv = -1;
}

GxTex *gx_texture(GxFormat f, int w, int h, int samples, unsigned usage) {
  GxTex *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  t->fmt = dx_format(f), t->w = w, t->h = h, t->samples = samples < 1 ? 1 : samples, t->usage = usage;
  /* a depth buffer the shaders read has a typeless format: its depth view and its shader view name the types */
  DXGI_FORMAT rf = t->fmt == DXGI_FORMAT_D32_FLOAT && (usage & GX_TEX_SAMPLED) ? DXGI_FORMAT_R32_TYPELESS : t->fmt;
  D3D12_RESOURCE_DESC d = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)w, (UINT)h, 1, 1, rf, { (UINT)t->samples, 0 },
                            D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
  if (usage & GX_TEX_COLOR) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (usage & GX_TEX_DEPTH) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if ((usage & GX_TEX_DEPTH) && !(usage & GX_TEX_SAMPLED)) d.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  t->state = usage & GX_TEX_SAMPLED ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
             : usage & GX_TEX_DEPTH ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                    : D3D12_RESOURCE_STATE_RENDER_TARGET;
  t->res = committed(D3D12_HEAP_TYPE_DEFAULT, &d, t->state);
  if (!t->res) return free(t), NULL;
  views_for(t);
  return t;
}

void gx_texture_free(GxTex *t) {
  if (!t) return;
  if (rb.t == t) rb.t = NULL;
  free_views(t);
  if (!t->swap) bury((IUnknown *)t->res);
  free(t);
}

GxBuf *gx_buffer(unsigned usage, uint32_t size) {
  (void)usage;
  GxBuf *b = calloc(1, sizeof *b);
  if (!b) return NULL;
  b->res = buffer_res(D3D12_HEAP_TYPE_DEFAULT, size ? size : 4, D3D12_RESOURCE_STATE_COMMON);
  b->size = size;
  if (!b->res) return free(b), NULL;
  return b;
}

void gx_buffer_free(GxBuf *b) {
  if (!b) return;
  bury((IUnknown *)b->res);
  free(b);
}

GxSampler *gx_sampler(bool linear, bool compare) {
  GxSampler *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->slot = take(used_smp, CPU_SMP);
  D3D12_SAMPLER_DESC d;
  memset(&d, 0, sizeof d);
  d.Filter = compare ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
  d.AddressU = d.AddressV = d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  d.MaxAnisotropy = 1;
  d.ComparisonFunc = compare ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
  d.MaxLOD = 1000;
  if (s->slot >= 0) ID3D12Device_CreateSampler(dev, &d, cpu_at(h_smp, inc_smp, s->slot));
  return s;
}

void gx_sampler_free(GxSampler *s) {
  if (!s) return;
  if (s->slot >= 0) used_smp[s->slot] = false;
  free(s);
}

bool gx_supports_samples(GxFormat f, int n) {
  if (n <= 1) return true;
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q = { dx_format(f), (UINT)n, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0 };
  if (FAILED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof q)) || !q.NumQualityLevels)
    return false;
  return f == GX_D32F || gx_supports_samples(GX_D32F, n);
}

/* ---- pipelines ------------------------------------------------------------------------------------ */
GxPipe *gx_pipeline(const GxPipeDesc *d) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC p;
  memset(&p, 0, sizeof p);
  p.pRootSignature = root;
  p.VS = (D3D12_SHADER_BYTECODE){ d->vs_code, d->vs_size };
  p.PS = (D3D12_SHADER_BYTECODE){ d->fs_code, d->fs_size };
  D3D12_RENDER_TARGET_BLEND_DESC *b = &p.BlendState.RenderTarget[0];
  b->SrcBlend = b->SrcBlendAlpha = D3D12_BLEND_ONE;
  b->DestBlend = b->DestBlendAlpha = D3D12_BLEND_ZERO;
  b->BlendOp = b->BlendOpAlpha = D3D12_BLEND_OP_ADD;
  b->LogicOp = D3D12_LOGIC_OP_NOOP;
  b->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  if (d->blend_premul) {
    b->BlendEnable = TRUE;
    b->DestBlend = b->DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  }
  p.SampleMask = 0xffffffffu;
  p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  p.RasterizerState.FrontCounterClockwise = TRUE;
  p.RasterizerState.DepthClipEnable = TRUE;
  if (d->depth_bias) {
    p.RasterizerState.DepthBias = 2;
    p.RasterizerState.SlopeScaledDepthBias = 2.5f;
  }
  p.DepthStencilState.DepthEnable = d->depth_test;
  p.DepthStencilState.DepthWriteMask = d->depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
  p.DepthStencilState.DepthFunc = d->depth_less_equal ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  D3D12_INPUT_ELEMENT_DESC el[4];
  for (int i = 0; i < d->nattrs && i < 4; i++) {
    const GxAttr *a = &d->attrs[i];
    el[i] = (D3D12_INPUT_ELEMENT_DESC){ "TEXCOORD", (UINT)a->location,
                                        a->format == GX_F32x3 ? DXGI_FORMAT_R32G32B32_FLOAT
                                        : a->format == GX_F32x4 ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                                                : DXGI_FORMAT_R8G8B8A8_UNORM,
                                        0, a->offset,
                                        d->per_instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                        d->per_instance ? 1u : 0u };
  }
  p.InputLayout = (D3D12_INPUT_LAYOUT_DESC){ d->nattrs ? el : NULL, (UINT)d->nattrs };
  p.PrimitiveTopologyType = d->prim == GX_LINES ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  p.NumRenderTargets = d->has_color ? 1 : 0;
  if (d->has_color) p.RTVFormats[0] = dx_format(d->color_format);
  p.DSVFormat = d->has_depth ? dx_format(d->depth_format) : DXGI_FORMAT_UNKNOWN;
  p.SampleDesc.Count = (UINT)(d->samples < 1 ? 1 : d->samples);
  GxPipe *g = calloc(1, sizeof *g);
  if (!g) return NULL;
  HRESULT hr = ID3D12Device_CreateGraphicsPipelineState(dev, &p, &IID_ID3D12PipelineState, (void **)&g->ps);
  if (FAILED(hr)) {
    fail("CreateGraphicsPipelineState", hr);
    debug_messages();
    free(g);
    return NULL;
  }
  g->topo = d->prim == GX_LINES ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  g->stride = d->stride;
  return g;
}

void gx_pipeline_free(GxPipe *p) {
  if (!p) return;
  bury((IUnknown *)p->ps);
  free(p);
}

/* ---- the device ---------------------------------------------------------------------------------- */
static bool make_root(void) {
  /* the slots of gx.h: b0-b1 space1 (vertex), t0-t3 and s0-s3 space2, b0-b1 space3 (pixel) */
  D3D12_DESCRIPTOR_RANGE r_srv = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 2, 0 };
  D3D12_DESCRIPTOR_RANGE r_smp = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 4, 0, 2, 0 };
  D3D12_ROOT_PARAMETER rp[6];
  memset(rp, 0, sizeof rp);
  for (int i = 0; i < 2; i++) {
    rp[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[i].Descriptor.ShaderRegister = (UINT)i;
    rp[i].Descriptor.RegisterSpace = 1;
    rp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rp[4 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[4 + i].Descriptor.ShaderRegister = (UINT)i;
    rp[4 + i].Descriptor.RegisterSpace = 3;
    rp[4 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  rp[2].ParameterType = rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rp[2].DescriptorTable = (D3D12_ROOT_DESCRIPTOR_TABLE){ 1, &r_srv };
  rp[3].DescriptorTable = (D3D12_ROOT_DESCRIPTOR_TABLE){ 1, &r_smp };
  rp[2].ShaderVisibility = rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC rd = { 6, rp, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
  HRESULT(WINAPI * ser)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **) =
      (void *)GetProcAddress(lib_d3d, "D3D12SerializeRootSignature");
  ID3DBlob *blob = NULL, *errs = NULL;
  HRESULT hr = ser ? ser(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs) : E_FAIL;
  if (errs) ID3D10Blob_Release(errs);
  if (FAILED(hr)) return fail("D3D12SerializeRootSignature", hr), false;
  hr = ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                        &IID_ID3D12RootSignature, (void **)&root);
  ID3D10Blob_Release(blob);
  if (FAILED(hr)) return fail("CreateRootSignature", hr), false;
  return true;
}

static void chain_views(void) {
  for (UINT i = 0; i < 3; i++) {
    GxTex *t = &chain_tex[i];
    IDXGISwapChain3_GetBuffer(chain, i, &IID_ID3D12Resource, (void **)&t->res);
    t->fmt = DXGI_FORMAT_B8G8R8A8_UNORM, t->w = chain_w, t->h = chain_h, t->samples = 1, t->usage = GX_TEX_COLOR;
    t->state = D3D12_RESOURCE_STATE_PRESENT, t->swap = true;
    views_for(t);
  }
}

static void chain_release(void) {
  for (int i = 0; i < 3; i++) {
    free_views(&chain_tex[i]);
    if (chain_tex[i].res) ID3D12Resource_Release(chain_tex[i].res);
    chain_tex[i].res = NULL;
  }
}

static bool make_chain(int w, int h) {
  DXGI_SWAP_CHAIN_DESC1 d;
  memset(&d, 0, sizeof d);
  d.Width = (UINT)w, d.Height = (UINT)h;
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  d.BufferCount = 3;
  d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  IDXGISwapChain1 *c1 = NULL;
  HRESULT hr = IDXGIFactory4_CreateSwapChainForHwnd(factory, (IUnknown *)queue, hwnd, &d, NULL, NULL, &c1);
  if (FAILED(hr)) return fail("CreateSwapChainForHwnd", hr), false;
  hr = IDXGISwapChain1_QueryInterface(c1, &IID_IDXGISwapChain3, (void **)&chain);
  IDXGISwapChain1_Release(c1);
  if (FAILED(hr)) return fail("IDXGISwapChain3", hr), false;
  IDXGIFactory4_MakeWindowAssociation(factory, hwnd, DXGI_MWA_NO_ALT_ENTER); /* the game does its own fullscreen */
  chain_w = w, chain_h = h;
  chain_views();
  return true;
}

static bool make_frames(void) {
  for (int f = 0; f < FRAMES; f++) {
    Frame *F = &frames[f];
    HRESULT hr = ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&F->alloc);
    if (FAILED(hr)) return fail("CreateCommandAllocator", hr), false;
    F->ring = buffer_res(D3D12_HEAP_TYPE_UPLOAD, RING, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!F->ring) return false;
    D3D12_RANGE none = { 0, 0 };
    ID3D12Resource_Map(F->ring, 0, &none, (void **)&F->ring_map);
    F->views = heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, VIEWS, true);
    F->samps = heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SAMPS, true);
    if (!F->views || !F->samps) return false;
  }
  HRESULT hr = ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[0].alloc, NULL,
                                              &IID_ID3D12GraphicsCommandList, (void **)&cl);
  if (FAILED(hr)) return fail("CreateCommandList", hr), false;
  ID3D12GraphicsCommandList_Close(cl);
  return true;
}

bool gx_open(const void *native_window, int w, int h, bool want_vsync) {
  err[0] = 0;
  const PlatNative *n = native_window;
  hwnd = n ? n->window : NULL;
  vsync = want_vsync;
  lib_d3d = LoadLibraryW(L"d3d12.dll");
  lib_dxgi = LoadLibraryW(L"dxgi.dll");
  if (!lib_d3d || !lib_dxgi) return fail("no d3d12.dll", 0), gx_close(), false;
  HRESULT(WINAPI * create_factory)(UINT, REFIID, void **) = (void *)GetProcAddress(lib_dxgi, "CreateDXGIFactory2");
  HRESULT(WINAPI * create_device)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **) = (void *)GetProcAddress(lib_d3d, "D3D12CreateDevice");
  HRESULT(WINAPI * get_debug)(REFIID, void **) = (void *)GetProcAddress(lib_d3d, "D3D12GetDebugInterface");
  if (!create_factory || !create_device) return fail("no D3D12CreateDevice", 0), gx_close(), false;
  if (getenv("TL_D3D_DEBUG") && get_debug) {
    ID3D12Debug *dbg = NULL;
    if (SUCCEEDED(get_debug(&IID_ID3D12Debug, (void **)&dbg))) {
      ID3D12Debug_EnableDebugLayer(dbg);
      ID3D12Debug_Release(dbg);
      debug_on = true;
    }
    plat_log("gpu: debug layer %s", debug_on ? "on" : "not installed");
  }
  HRESULT hr = create_factory(debug_on ? DXGI_CREATE_FACTORY_DEBUG : 0, &IID_IDXGIFactory4, (void **)&factory);
  if (FAILED(hr)) hr = create_factory(0, &IID_IDXGIFactory4, (void **)&factory);
  if (FAILED(hr)) return fail("CreateDXGIFactory2", hr), gx_close(), false;
  /* the first hardware adapter that has Direct3D 12, else the system's software one (WARP) */
  IDXGIAdapter1 *ad = NULL;
  for (UINT i = 0; !dev && SUCCEEDED(IDXGIFactory4_EnumAdapters1(factory, i, &ad)); i++) {
    DXGI_ADAPTER_DESC1 ds;
    IDXGIAdapter1_GetDesc1(ad, &ds);
    if (!(ds.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(create_device((IUnknown *)ad, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev)))
      WideCharToMultiByte(CP_UTF8, 0, ds.Description, -1, drvname + 11, (int)sizeof drvname - 12, NULL, NULL), drvname[10] = ' ';
    IDXGIAdapter1_Release(ad);
  }
  if (!dev) {
    IDXGIAdapter *warp = NULL;
    if (SUCCEEDED(IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&warp))) {
      create_device((IUnknown *)warp, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
      IDXGIAdapter_Release(warp);
      if (dev) plat_log("gpu: no Direct3D 12 hardware adapter: on WARP, the system's software renderer (slow)");
    }
  }
  snprintf(drvname, sizeof drvname, "direct3d12"); /* the menu shows the API's name, as before */
  if (!dev) return fail("no Direct3D 12 device", 0), gx_close(), false;
  if (debug_on) ID3D12Device_QueryInterface(dev, &IID_ID3D12InfoQueue, (void **)&info);
  D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
  hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&queue);
  if (FAILED(hr)) return fail("CreateCommandQueue", hr), gx_close(), false;
  hr = ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
  if (FAILED(hr)) return fail("CreateFence", hr), gx_close(), false;
  fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);
  inc_view = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  inc_rtv = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  inc_dsv = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  inc_smp = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  h_srv = heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CPU_SRV, false);
  h_rtv = heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, CPU_RTV, false);
  h_dsv = heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, CPU_DSV, false);
  h_smp = heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, CPU_SMP, false);
  if (!h_srv || !h_rtv || !h_dsv || !h_smp || !make_root() || !make_frames()) return gx_close(), false;
  if (hwnd) {
    RECT r;
    GetClientRect(hwnd, &r);
    if (!make_chain(r.right > 0 ? r.right : 1, r.bottom > 0 ? r.bottom : 1)) return gx_close(), false;
  } else {
    offscreen = gx_texture(GX_BGRA8, w > 0 ? w : 1280, h > 0 ? h : 720, 1, GX_TEX_COLOR | GX_TEX_SAMPLED);
    if (!offscreen) return gx_close(), false;
  }
  return true;
}

void gx_close(void) {
  if (queue && fence) gx_wait_idle();
  for (int f = 0; f < FRAMES; f++) reap(&frames[f]);
  if (offscreen) gx_texture_free(offscreen);
  offscreen = NULL;
  for (int f = 0; f < FRAMES; f++) reap(&frames[f]);
  if (rb.buf) ID3D12Resource_Release(rb.buf);
  memset(&rb, 0, sizeof rb);
  chain_release();
  if (chain) IDXGISwapChain3_Release(chain);
  chain = NULL;
  for (int f = 0; f < FRAMES; f++) {
    Frame *F = &frames[f];
    IUnknown *all[] = { (IUnknown *)F->alloc, (IUnknown *)F->ring, (IUnknown *)F->views, (IUnknown *)F->samps, (IUnknown *)F->stage };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
      if (all[i]) IUnknown_Release(all[i]);
    memset(F, 0, sizeof *F);
  }
  IUnknown *all[] = { (IUnknown *)cl, (IUnknown *)root, (IUnknown *)h_srv, (IUnknown *)h_rtv, (IUnknown *)h_dsv,
                      (IUnknown *)h_smp, (IUnknown *)fence, (IUnknown *)queue, (IUnknown *)info, (IUnknown *)dev,
                      (IUnknown *)factory };
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    if (all[i]) IUnknown_Release(all[i]);
  cl = NULL, root = NULL, h_srv = h_rtv = h_dsv = h_smp = NULL, fence = NULL, queue = NULL, info = NULL, dev = NULL, factory = NULL;
  memset(used_srv, 0, sizeof used_srv), memset(used_rtv, 0, sizeof used_rtv);
  memset(used_dsv, 0, sizeof used_dsv), memset(used_smp, 0, sizeof used_smp);
  if (fence_event) CloseHandle(fence_event);
  fence_event = NULL;
  recording = false;
  fi = 0;
}

/* ---- the frame ------------------------------------------------------------------------------------- */
static void transition(GxTex *t, D3D12_RESOURCE_STATES to) {
  if (t->state == to) return;
  D3D12_RESOURCE_BARRIER b;
  memset(&b, 0, sizeof b);
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = t->res;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = t->state;
  b.Transition.StateAfter = to;
  ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
  t->state = to;
}

static void buffer_barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER b;
  memset(&b, 0, sizeof b);
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = r;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = from;
  b.Transition.StateAfter = to;
  ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
}

static GxTex *frame_swap;

bool gx_frame_begin(GxTex **swap, int *w, int *h) {
  Frame *F = &frames[fi];
  wait_value(F->fence_value);
  reap(F);
  debug_messages();
  if (chain) {
    RECT r;
    GetClientRect(hwnd, &r);
    if (r.right <= 0 || r.bottom <= 0) return false; /* minimised */
    if (r.right != chain_w || r.bottom != chain_h) {
      gx_wait_idle();
      for (int f = 0; f < FRAMES; f++) reap(&frames[f]);
      chain_release();
      HRESULT hr = IDXGISwapChain3_ResizeBuffers(chain, 3, (UINT)r.right, (UINT)r.bottom, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
      if (FAILED(hr)) return fail("ResizeBuffers", hr), false;
      chain_w = r.right, chain_h = r.bottom;
      chain_views();
    }
    frame_swap = &chain_tex[IDXGISwapChain3_GetCurrentBackBufferIndex(chain)];
  } else {
    frame_swap = offscreen;
  }
  ID3D12CommandAllocator_Reset(F->alloc);
  ID3D12GraphicsCommandList_Reset(cl, F->alloc, NULL);
  ID3D12DescriptorHeap *hs[2] = { F->views, F->samps };
  ID3D12GraphicsCommandList_SetDescriptorHeaps(cl, 2, hs);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(cl, root);
  F->ring_used = F->nviews = F->nsamps = 0;
  F->stage_used = 0;
  D3D12_GPU_VIRTUAL_ADDRESS base = ID3D12Resource_GetGPUVirtualAddress(F->ring);
  for (int s = 0; s < 2; s++)
    for (int k = 0; k < 2; k++) ucb[s][k] = base;
  recording = true;
  cur_pipe = NULL;
  *swap = frame_swap;
  *w = frame_swap->w;
  *h = frame_swap->h;
  return true;
}

static uint8_t *stage(uint32_t size, uint32_t align, ID3D12Resource **res, uint32_t *off) {
  Frame *F = &frames[fi];
  uint32_t at = (F->stage_used + align - 1) & ~(align - 1);
  if (!F->stage || at + size > F->stage_size) {
    uint32_t cap = F->stage_size ? F->stage_size * 2 : 4u << 20;
    while (cap < size + align) cap *= 2;
    if (F->stage) bury((IUnknown *)F->stage); /* this frame's earlier copies still read it */
    F->stage = buffer_res(D3D12_HEAP_TYPE_UPLOAD, cap, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!F->stage) return F->stage_size = 0, NULL;
    D3D12_RANGE none = { 0, 0 };
    ID3D12Resource_Map(F->stage, 0, &none, (void **)&F->stage_map);
    F->stage_size = cap;
    at = 0;
  }
  F->stage_used = at + size;
  *res = F->stage;
  *off = at;
  return F->stage_map + at;
}

void gx_upload_buffer(GxBuf *b, const void *data, uint32_t size) {
  if (!b || !size || !recording) return;
  ID3D12Resource *sr;
  uint32_t off;
  uint8_t *p = stage(size, 256, &sr, &off);
  if (!p) return;
  memcpy(p, data, size);
  buffer_barrier(b->res, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  ID3D12GraphicsCommandList_CopyBufferRegion(cl, b->res, 0, sr, off, size);
  buffer_barrier(b->res, D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER);
  /* the next frame's command list finds it decayed to the common state again (a buffer always does) */
}

void gx_upload_texture(GxTex *t, const void *px, int w, int h) {
  if (!t || !recording) return;
  uint32_t pitch = ((uint32_t)w * 4 + 255) & ~255u;
  ID3D12Resource *sr;
  uint32_t off;
  uint8_t *p = stage(pitch * (uint32_t)h, 512, &sr, &off);
  if (!p) return;
  for (int y = 0; y < h; y++) memcpy(p + (size_t)y * pitch, (const uint8_t *)px + (size_t)y * (size_t)w * 4, (size_t)w * 4);
  transition(t, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION dst, src;
  memset(&dst, 0, sizeof dst);
  memset(&src, 0, sizeof src);
  dst.pResource = t->res;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.pResource = sr;
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Offset = off;
  src.PlacedFootprint.Footprint = (D3D12_SUBRESOURCE_FOOTPRINT){ t->fmt, (UINT)w, (UINT)h, 1, pitch };
  ID3D12GraphicsCommandList_CopyTextureRegion(cl, &dst, 0, 0, 0, &src, NULL);
  transition(t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void gx_viewport(float x, float y, float w, float h) {
  D3D12_VIEWPORT v = { x, y, w, h, 0, 1 };
  ID3D12GraphicsCommandList_RSSetViewports(cl, 1, &v);
}

void gx_scissor(int x, int y, int w, int h) {
  D3D12_RECT r = { x, y, x + w, y + h };
  ID3D12GraphicsCommandList_RSSetScissorRects(cl, 1, &r);
}

void gx_pass_begin(const GxPass *p) {
  pass_color = p->color, pass_resolve = p->resolve, pass_depth = p->depth;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv, dsv;
  if (p->color) {
    transition(p->color, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv = cpu_at(h_rtv, inc_rtv, p->color->rtv);
    if (p->color_load == GX_LOAD_CLEAR) ID3D12GraphicsCommandList_ClearRenderTargetView(cl, rtv, p->clear, 0, NULL);
  }
  if (p->depth) {
    transition(p->depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    dsv = cpu_at(h_dsv, inc_dsv, p->depth->dsv);
    if (p->depth_load == GX_LOAD_CLEAR)
      ID3D12GraphicsCommandList_ClearDepthStencilView(cl, dsv, D3D12_CLEAR_FLAG_DEPTH, p->clear_depth, 0, 0, NULL);
  }
  ID3D12GraphicsCommandList_OMSetRenderTargets(cl, p->color ? 1 : 0, p->color ? &rtv : NULL, FALSE, p->depth ? &dsv : NULL);
  GxTex *s = p->color ? p->color : p->depth;
  if (!s) return;
  gx_viewport(0, 0, (float)s->w, (float)s->h);
  gx_scissor(0, 0, s->w, s->h);
}

void gx_pass_end(void) {
  if (pass_resolve && pass_color) {
    transition(pass_color, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    transition(pass_resolve, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ID3D12GraphicsCommandList_ResolveSubresource(cl, pass_resolve->res, 0, pass_color->res, 0, pass_color->fmt);
  }
  /* what will be sampled goes to the read state now */
  GxTex *all[3] = { pass_color, pass_resolve, pass_depth };
  for (int i = 0; i < 3; i++)
    if (all[i] && (all[i]->usage & GX_TEX_SAMPLED)) transition(all[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  pass_color = pass_resolve = pass_depth = NULL;
}

void gx_bind_pipeline(GxPipe *p) {
  ID3D12GraphicsCommandList_SetPipelineState(cl, p->ps);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(cl, p->topo);
  cur_pipe = p;
}

void gx_bind_vertex(GxBuf *b) {
  D3D12_VERTEX_BUFFER_VIEW v = { ID3D12Resource_GetGPUVirtualAddress(b->res), b->size, cur_pipe ? cur_pipe->stride : 0 };
  ID3D12GraphicsCommandList_IASetVertexBuffers(cl, 0, 1, &v);
}

void gx_bind_index32(GxBuf *b) {
  D3D12_INDEX_BUFFER_VIEW v = { ID3D12Resource_GetGPUVirtualAddress(b->res), b->size, DXGI_FORMAT_R32_UINT };
  ID3D12GraphicsCommandList_IASetIndexBuffer(cl, &v);
}

void gx_bind_textures(GxTex *const *t, GxSampler *const *s, int n) {
  Frame *F = &frames[fi];
  if (n < 1 || F->nviews + 4 > VIEWS || F->nsamps + 4 > SAMPS) return;
  for (int i = 0; i < 4; i++) {
    int k = i < n ? i : 0; /* unused slots repeat the first */
    ID3D12Device_CopyDescriptorsSimple(dev, 1, cpu_at(F->views, inc_view, F->nviews + i), cpu_at(h_srv, inc_view, t[k]->srv),
                                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Device_CopyDescriptorsSimple(dev, 1, cpu_at(F->samps, inc_smp, F->nsamps + i), cpu_at(h_smp, inc_smp, s[k]->slot),
                                       D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  }
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cl, 2, gpu_at(F->views, inc_view, F->nviews));
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cl, 3, gpu_at(F->samps, inc_smp, F->nsamps));
  F->nviews += 4, F->nsamps += 4;
}

void gx_uniform(int st, int slot, const void *data, uint32_t size) {
  Frame *F = &frames[fi];
  if (size > 512 || F->ring_used + 512 > RING) return;
  memcpy(F->ring_map + F->ring_used, data, size);
  ucb[st & 1][slot & 1] = ID3D12Resource_GetGPUVirtualAddress(F->ring) + F->ring_used;
  F->ring_used += 512; /* room for the largest a shader reads, 256-aligned */
}

static void flush_uniforms(void) {
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(cl, 0, ucb[0][0]);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(cl, 1, ucb[0][1]);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(cl, 4, ucb[1][0]);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(cl, 5, ucb[1][1]);
}

void gx_draw(uint32_t verts, uint32_t instances, uint32_t first_vert, uint32_t first_instance) {
  flush_uniforms();
  ID3D12GraphicsCommandList_DrawInstanced(cl, verts, instances, first_vert, first_instance);
}

void gx_draw_indexed(uint32_t indices) {
  flush_uniforms();
  ID3D12GraphicsCommandList_DrawIndexedInstanced(cl, indices, 1, 0, 0, 0);
}

void gx_readback(GxTex *t, void *out) {
  rb.t = t;
  rb.out = out;
}

void gx_frame_end(void) {
  Frame *F = &frames[fi];
  bool reading = rb.t && rb.out;
  if (reading) {
    rb.pitch = ((uint32_t)rb.t->w * 4 + 255) & ~255u;
    uint32_t size = rb.pitch * (uint32_t)rb.t->h;
    if (rb.size < size) {
      if (rb.buf) ID3D12Resource_Release(rb.buf);
      rb.buf = buffer_res(D3D12_HEAP_TYPE_READBACK, size, D3D12_RESOURCE_STATE_COPY_DEST);
      rb.size = rb.buf ? size : 0;
    }
    reading = rb.buf != NULL;
    if (reading) {
      transition(rb.t, D3D12_RESOURCE_STATE_COPY_SOURCE);
      D3D12_TEXTURE_COPY_LOCATION dst, src;
      memset(&dst, 0, sizeof dst);
      memset(&src, 0, sizeof src);
      src.pResource = rb.t->res;
      src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst.pResource = rb.buf;
      dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      dst.PlacedFootprint.Footprint = (D3D12_SUBRESOURCE_FOOTPRINT){ rb.t->fmt, (UINT)rb.t->w, (UINT)rb.t->h, 1, rb.pitch };
      ID3D12GraphicsCommandList_CopyTextureRegion(cl, &dst, 0, 0, 0, &src, NULL);
    }
  }
  transition(frame_swap, chain ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  ID3D12GraphicsCommandList_Close(cl);
  ID3D12CommandList *lists[1] = { (ID3D12CommandList *)cl };
  ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
  recording = false;
  if (chain) {
    bool wait = vsync && plat_core_pace_probe(); /* vsync where the display keeps up (plat_core.c) */
    uint64_t t0 = plat_counter();
    HRESULT hr = IDXGISwapChain3_Present(chain, wait ? 1 : 0, 0);
    if (wait) plat_core_pace_note(plat_counter() - t0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) plat_log("gpu: the device was lost (0x%08lx)", (unsigned long)hr);
  }
  F->fence_value = fence_next++;
  ID3D12CommandQueue_Signal(queue, fence, F->fence_value);
  if (reading) {
    wait_value(F->fence_value);
    D3D12_RANGE all = { 0, rb.size };
    uint8_t *m = NULL;
    if (SUCCEEDED(ID3D12Resource_Map(rb.buf, 0, &all, (void **)&m)) && m) {
      for (int y = 0; y < rb.t->h; y++)
        memcpy((uint8_t *)rb.out + (size_t)y * (size_t)rb.t->w * 4, m + (size_t)y * rb.pitch, (size_t)rb.t->w * 4);
      D3D12_RANGE none = { 0, 0 };
      ID3D12Resource_Unmap(rb.buf, 0, &none);
    }
  }
  rb.t = NULL;
  rb.out = NULL;
  fi = (fi + 1) % FRAMES;
}
