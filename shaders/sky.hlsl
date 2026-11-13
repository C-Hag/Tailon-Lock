// sky.hlsl - the sky behind everything: a gradient, the sun's glow and
// disc, and stars at night, along each pixel's view ray.
#include "common.hlsli"

[[vk::binding(0, 3)]] cbuffer ViewF : register(b0, space3) { ViewData VF; };

struct VSOut {
  float4 clip : SV_Position;
  [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID) {
  VSOut o;
  float2 p = float2((id << 1) & 2, id & 2) * 2 - 1;
  o.clip = float4(p, 0, 1);
  o.ndc = p;
  return o;
}

float4 fs_main(VSOut i) : SV_Target0 {
  ViewData v = VF;
  float3 d = normalize(v.camFwd.xyz + v.camRight.xyz * i.ndc.x * v.camRight.w + v.camUp.xyz * i.ndc.y * v.camUp.w);
  float3 c = sky_color(v, d, true);
  if (v.skyTop.w > 0.01 && d.y > 0.02) {
    float2 q = float2(atan2(d.x, d.z) * 380, d.y * 600);
    float2 cell = floor(q);
    float h = hash21(cell);
    float tw = 0.7 + 0.3 * sin(v.camPos.w * 3 + h * 40);
    float2 f = frac(q) - 0.5;
    if (h > 0.985) c += v.skyTop.w * tw * (1 - smoothstep(0.0, 0.35, length(f))) * (h - 0.985) * 120;
  }
  return float4(c, 1);
}
