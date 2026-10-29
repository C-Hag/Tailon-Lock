// line.hlsl - tracers, missile bodies and the tanker's hose.
#include "common.hlsli"

[[vk::binding(0, 1)]] cbuffer ViewV : register(b0, space1) { ViewData VV; };

struct VSIn {
  [[vk::location(0)]] float3 pos : TEXCOORD0;
  [[vk::location(1)]] float4 col : TEXCOORD1; // linear HDR rgb, alpha
};
struct VSOut {
  float4 clip : SV_Position;
  [[vk::location(0)]] float4 col : TEXCOORD0;
};

VSOut vs_main(VSIn i) {
  VSOut o;
  o.clip = mul(VV.viewProj, float4(i.pos, 1));
  o.col = i.col;
  return o;
}

float4 fs_main(VSOut i) : SV_Target0 { return float4(i.col.rgb * i.col.a, i.col.a); }
