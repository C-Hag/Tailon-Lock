// sprite.hlsl - camera-facing quads: smoke and debris, glowing fire,
// flares and lights (additive), and sunlit cloud puffs shaded as spheres.
// Premultiplied alpha, so an additive sprite is just one with alpha 0.
#include "common.hlsli"

[[vk::binding(0, 1)]] cbuffer ViewV : register(b0, space1) { ViewData VV; };
[[vk::binding(0, 3)]] cbuffer ViewF : register(b0, space3) { ViewData VF; };

struct VSIn {
  [[vk::location(0)]] float4 posSize : TEXCOORD0; // xyz, world radius
  [[vk::location(1)]] float4 col : TEXCOORD1;     // rgb, alpha
  [[vk::location(2)]] float4 par : TEXCOORD2;     // x: kind, y: min pixels, z: seed
};
struct VSOut {
  float4 clip : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float4 col : TEXCOORD1;
  [[vk::location(2)]] float4 par : TEXCOORD2;
  [[vk::location(3)]] float3 wp : TEXCOORD3;
};

VSOut vs_main(VSIn i, uint id : SV_VertexID) {
  const float2 C[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };
  float2 c = C[id % 6];
  float z = max(dot(i.posSize.xyz - VV.camPos.xyz, VV.camFwd.xyz), 0.5);
  float r = max(i.posSize.w, i.par.y * 0.5 * z / VV.camFwd.w);
  float3 w = i.posSize.xyz + (VV.camRight.xyz * c.x + VV.camUp.xyz * c.y) * r;
  VSOut o;
  o.clip = mul(VV.viewProj, float4(w, 1));
  o.uv = c;
  o.col = i.col;
  o.par = i.par;
  o.wp = i.posSize.xyz;
  return o;
}

float4 fs_main(VSOut i) : SV_Target0 {
  ViewData v = VF;
  float r2 = dot(i.uv, i.uv);
  if (r2 > 1) discard;
  int kind = (int)i.par.x;
  float3 c = to_linear(i.col.rgb);
  float a = i.col.a;
  if (kind == 1) {
    // cloud puff: a lit sphere with a ragged rim
    float3 n = normalize(v.camRight.xyz * i.uv.x + v.camUp.xyz * i.uv.y - v.camFwd.xyz * sqrt(1 - r2));
    float lit = 0.72 + 0.4 * saturate(dot(n, normalize(v.sunDir.xyz)));
    float rim = vnoise(i.uv * 3 + i.par.z * 17) * 0.35;
    a *= smoothstep(1.0, 0.55 - rim * 0.3, r2 + rim * 0.35);
    c = apply_fog(v, c * lit, i.wp);
    return float4(c * a, a);
  }
  if (kind == 2) {
    // glow: hot core, added on top
    float g = pow(1 - r2, 2.2);
    return float4(c * g * a * 3.0, 0);
  }
  // soft particle
  a *= pow(1 - r2, 1.4);
  c = apply_fog(v, c, i.wp);
  return float4(c * a, a);
}
