// world.hlsl - meshes, terrain, runways and the far plane: sun and sky
// light, specular, a 3x3 PCF sun shadow, procedural ground detail, the
// ocean (waves, Fresnel sky reflection, sun glitter, foam) and fog.
#include "common.hlsli"

[[vk::binding(0, 1)]] cbuffer ViewV : register(b0, space1) { ViewData VV; };
[[vk::binding(1, 1)]] cbuffer InstV : register(b1, space1) {
  float4 iR, iU, iF, iPos;   // basis; iPos.w: scale
  float4 iTint;              // x: dim, y: gloss, z: ground detail, w: cull box on
  float4 iCullMin, iCullMax;
};
[[vk::binding(0, 3)]] cbuffer ViewF : register(b0, space3) { ViewData VF; };
[[vk::binding(1, 3)]] cbuffer InstF : register(b1, space3) {
  float4 fR, fU, fF, fPos;
  float4 fTint;
  float4 fCullMin, fCullMax;
};
[[vk::combinedImageSampler]][[vk::binding(0, 2)]] Texture2D ShadowTex : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]] SamplerComparisonState ShadowSmp : register(s0, space2);

struct VSIn {
  [[vk::location(0)]] float3 pos : TEXCOORD0;
  [[vk::location(1)]] float3 nrm : TEXCOORD1;
  [[vk::location(2)]] float4 col : TEXCOORD2;
};
struct VSOut {
  float4 clip : SV_Position;
  [[vk::location(0)]] float3 wp : TEXCOORD0;
  [[vk::location(1)]] float3 n : TEXCOORD1;
  [[vk::location(2)]] float4 col : TEXCOORD2;
  [[vk::location(3)]] float3 lp : TEXCOORD3;
};

VSOut vs_main(VSIn i) {
  VSOut o;
  float3 w = iPos.xyz + (iR.xyz * i.pos.x + iU.xyz * i.pos.y + iF.xyz * i.pos.z) * iPos.w;
  o.clip = mul(VV.viewProj, float4(w, 1));
  o.wp = w;
  o.n = iR.xyz * i.nrm.x + iU.xyz * i.nrm.y + iF.xyz * i.nrm.z;
  o.col = i.col;
  o.lp = i.pos;
  return o;
}

float shadow_at(float3 wp, float3 n) {
  if (VF.sunCol.w < 0.5) return 1;
  float4 s = mul(VF.shadowVP, float4(wp + n * 0.6, 1));
  float2 uv = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
  if (any(uv < 0) || any(uv > 1) || s.z > 1) return 1;
  float t = VF.misc.z, sum = 0;
  [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++)
      sum += ShadowTex.SampleCmpLevelZero(ShadowSmp, uv + float2(x, y) * t, s.z - 0.0015);
  return sum / 9;
}

// The ocean's height: two swells plus drifting value noise at three
// scales (noise keeps the sea from tiling into a grid). The slope comes
// from finite differences.
float sea_height(float2 p, float t) {
  float h = sin(dot(p, float2(0.011, 0.007)) + t * 0.8) * 0.6 + sin(dot(p, float2(-0.006, 0.015)) + t * 1.1) * 0.4;
  h += vnoise(p * 0.035 + float2(t * 0.05, t * 0.03)) * 1.1;
  h += vnoise(p * 0.11 - float2(t * 0.09, -t * 0.04)) * 0.45;
  h += vnoise(p * 0.37 + float2(t * 0.2, t * 0.13)) * 0.14;
  return h;
}

float2 wave_slope(float2 p, float t) {
  const float e = 0.8;
  float h0 = sea_height(p, t);
  return float2(sea_height(p + float2(e, 0), t) - h0, sea_height(p + float2(0, e), t) - h0) / e * 1.6;
}

float4 fs_main(VSOut i) : SV_Target0 {
  ViewData v = VF;
  if (fTint.w > 0.5 && all(i.lp > fCullMin.xyz) && all(i.lp < fCullMax.xyz)) discard;
  float3 V = normalize(v.camPos.xyz - i.wp);
  float3 L = normalize(v.sunDir.xyz);
  float3 N = normalize(i.n);
  if (dot(N, V) < 0) N = -N;
  float3 sun = to_linear(v.sunCol.rgb) * v.sunDir.w;
  float3 amb = to_linear(v.ambCol.rgb) * v.ambCol.w;
  float sh = shadow_at(i.wp, N);
  float3 c;
  if (i.col.a >= 0.5 && i.col.a < 0.9) {
    // glowing screens and lights: emitted, not lit
    c = to_linear(i.col.rgb) * 2.4;
  } else if (i.col.a < 0.5) {
    // water
    float dist = length(v.camPos.xyz - i.wp);
    float2 sl = wave_slope(i.wp.xz, v.camPos.w) * saturate(1.3 - dist / 7000);
    float3 Nw = normalize(float3(-sl.x, 1, -sl.y));
    // at grazing angles a wavy sea mirrors less than a calm one, and the sky above the haze: so the sea stays
    // the sea right up to the horizon instead of melting into the sky there
    float fres = 0.02 + 0.68 * pow(1 - saturate(dot(Nw, V)), 5);
    float3 R = reflect(-V, Nw);
    R.y = max(abs(R.y), 0.12);
    float3 deep = to_linear(i.col.rgb) * (amb * 0.6 + sun * 0.25 * saturate(L.y));
    c = lerp(deep, sky_color(v, R, false), saturate(fres));
    float3 H = normalize(L + V);
    c += sun * pow(saturate(dot(Nw, H)), 600) * 12 * sh;
    // whitecaps where the chop crests
    float foam = smoothstep(0.78, 0.98, vnoise(i.wp.xz * 0.06 + v.camPos.w * 0.15) * vnoise(i.wp.xz * 0.23 - v.camPos.w * 0.1) * 2.0);
    c = lerp(c, (amb + sun * 0.4) * 0.8, foam * 0.18 * saturate(1 - dist / 3000));
  } else {
    float3 albedo = to_linear(i.col.rgb) * fTint.x;
    if (fTint.z > 0.5) {
      // ground: two octaves of noise, rock on steep slopes
      float n1 = vnoise(i.wp.xz * 0.02), n2 = vnoise(i.wp.xz * 0.15), n3 = vnoise(i.wp.xz * 1.1);
      albedo *= 0.78 + 0.28 * n1 + 0.14 * n2 + 0.06 * n3;
      float rock = smoothstep(0.82, 0.6, N.y);
      albedo = lerp(albedo, float3(0.16, 0.15, 0.14) * (0.8 + 0.4 * n2), rock * 0.7);
    }
    float diff = saturate(dot(N, L));
    float3 hemi = amb * (0.55 + 0.45 * N.y) + to_linear(v.fogCol.rgb) * 0.12 * (1 - N.y);
    c = albedo * (hemi + sun * diff * sh);
    if (fTint.y > 0) {
      float3 H = normalize(L + V);
      float fres = 0.04 + 0.96 * pow(1 - saturate(dot(N, V)), 5);
      c += sun * pow(saturate(dot(N, H)), 48) * fTint.y * (0.25 + fres) * sh;
      // a hint of sky in glossy paint
      c += sky_color(v, reflect(-V, N), false) * fres * fTint.y * 0.25;
    }
  }
  if (v.misc.y < 0.5 || true) c = apply_fog(v, c, i.wp);
  return float4(c, 1);
}

// ---- shadow map: depth only ----
struct SVSOut { float4 clip : SV_Position; };
SVSOut vs_shadow(VSIn i) {
  SVSOut o;
  float3 w = iPos.xyz + (iR.xyz * i.pos.x + iU.xyz * i.pos.y + iF.xyz * i.pos.z) * iPos.w;
  o.clip = mul(VV.shadowVP, float4(w, 1));
  return o;
}
void fs_shadow() {}
