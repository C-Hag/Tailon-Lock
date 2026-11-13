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
  if (i.col.a >= 0.5 && i.col.a < 0.9 && fTint.z <= 0.5) { // (on the ground the alpha is the land's share: only its middle is the coast)
    // glowing screens and lights: emitted, not lit
    c = to_linear(i.col.rgb) * 2.4;
  } else if (i.col.a < 0.5) {
    // water
    float dist = length(v.camPos.xyz - i.wp);
    float2 sl = wave_slope(i.wp.xz, v.camPos.w) * saturate(1.3 - dist / 7000);
    // waves finer than a pixel cannot be drawn: they would only sparkle from frame to frame, through a long lens
    // most of all. Calm the slope and spread the sun's highlight (same energy) by how much sea one pixel covers.
    float seaPx = length(fwidth(i.wp.xz));
    bool fineSea = v.misc.w > 1.5; // at ULTRA and in the film: a long swell, wide enough to draw, and a shore
    bool filmSea = v.misc.w > 2.5; // the opening film's sea: seen from high up at dawn, it must not go flat and dark
    sl /= 1 + seaPx * (filmSea ? 1.2 : 0.3);
    if (fineSea) sl += wave_slope(i.wp.xz * 0.13, v.camPos.w * 0.45) * 0.55 / (1 + seaPx * 0.12);
    float3 Nw = normalize(float3(-sl.x, 1, -sl.y));
    // at grazing angles a wavy sea mirrors less than a calm one, and the sky above the haze: so the sea stays
    // the sea right up to the horizon instead of melting into the sky there
    float fres = 0.02 + 0.68 * pow(1 - saturate(dot(Nw, V)), 5);
    if (filmSea) fres = max(fres, 0.2);
    float3 R = reflect(-V, Nw);
    R.y = max(abs(R.y), 0.12);
    float3 deep = to_linear(i.col.rgb) * (amb * 0.6 + sun * 0.25 * saturate(L.y));
    if (filmSea) deep = float3(0.04, 0.12, 0.17) * (amb + sun * 0.55);
    c = lerp(deep, sky_color(v, R, false), saturate(fres));
    float3 H = normalize(L + V);
    float glint = 600 / (1 + seaPx * seaPx * 30);
    c += sun * pow(saturate(dot(Nw, H)), glint) * 12 * (glint / 600) * sh;
    // whitecaps where the chop crests
    float foam = smoothstep(0.78, 0.98, vnoise(i.wp.xz * 0.06 + v.camPos.w * 0.15) * vnoise(i.wp.xz * 0.23 - v.camPos.w * 0.1) * 2.0);
    if (filmSea) c += sun * pow(saturate(dot(R, L)), 90.0) * 0.28 * sh; // the sun's broad path on the swell
    if (fineSea && fTint.z > 0.5) {
      // towards the coast: lighter water over the sand, and a broken line of surf where it meets the land
      float shore = smoothstep(0.2, 0.5, i.col.a);
      c = lerp(c, float3(0.05, 0.30, 0.33) * (amb + sun * 0.55), shore * 0.55);
      float surf = smoothstep(0.46, 0.5, i.col.a) * smoothstep(0.35, 0.65, vnoise(i.wp.xz * 0.07 + v.camPos.w * 0.12));
      c = lerp(c, (amb + sun * 0.55) * 0.85, surf * 0.35);
    }
    if (fineSea) foam *= 0.28 * saturate(1.2 - seaPx * 0.8); // whitecaps smaller than a pixel only speckle
    c = lerp(c, (amb + sun * 0.4) * 0.8, foam * 0.18 * saturate(1 - dist / 3000));
  } else {
    float3 albedo = to_linear(i.col.rgb) * fTint.x;
    if (fTint.z > 0.5) {
      // ground: two octaves of noise, rock on steep slopes
      float n1 = vnoise(i.wp.xz * 0.02), n2 = vnoise(i.wp.xz * 0.15), n3 = vnoise(i.wp.xz * 1.1);
      albedo *= 0.78 + 0.28 * n1 + 0.14 * n2 + 0.06 * n3;
      float rock = 1 - smoothstep(0.6, 0.82, N.y);
      albedo = lerp(albedo, float3(0.16, 0.15, 0.14) * (0.8 + 0.4 * n2), rock * 0.7);
      if (v.misc.w > 1.5) albedo = lerp(albedo, float3(0.52, 0.42, 0.24) * (0.85 + 0.25 * n2), 1 - smoothstep(0.5, 0.6, i.col.a)); // the beach
    }
    float wet = 1;
    if (v.misc.w > 1.5 && fTint.y > 0) {
      // ULTRA, a jet's skin up close: panel lines, frames across the fuselage and ribs along it, in the model's own metres,
      // gone before a pixel gets too big to show them (no shimmer in the distance); weathering streaks with the airflow
      float3 pw = fwidth(i.lp), nl = abs(normalize(cross(ddx(i.lp), ddy(i.lp))));
      const float3 cell = float3(2.3, 0.9, 1.9);
      // each row of panels starts at its own place, as real skin does, so the lines are no grid
      float row = floor(i.lp.x / cell.x);
      float3 at = i.lp + float3(0, 0, 0.71 * row);
      float3 dist = (0.5 - abs(frac(at / cell) - 0.5)) * cell;
      float3 line_ = (1 - smoothstep((float3)0.007, (float3)0.007 + pw * 1.3, dist)) * (1 - nl);
      float show = saturate(1.5 - max(pw.x, max(pw.y, pw.z)) * 11);
      float ln = max(line_.x, max(line_.y, line_.z)) * show;
      float streak = vnoise(float2(i.lp.x * 6.5, i.lp.z * 0.55)), blotch = vnoise(i.lp.xz * 0.9 + i.lp.yy * 1.7);
      albedo *= (1 - 0.24 * ln) * (0.9 + 0.13 * streak) * (0.93 + 0.14 * blotch);
      wet = (0.65 + 0.7 * streak) * (1 - 0.7 * ln);
    }
    float diff = saturate(dot(N, L));
    float3 hemi = amb * (0.55 + 0.45 * N.y) + to_linear(v.fogCol.rgb) * 0.12 * (1 - N.y);
    c = albedo * (hemi + sun * diff * sh);
    if (fTint.y > 0) {
      float3 H = normalize(L + V);
      float fres = 0.04 + 0.96 * pow(1 - saturate(dot(N, V)), 5);
      c += sun * pow(saturate(dot(N, H)), 48) * fTint.y * wet * (0.25 + fres) * sh;
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
