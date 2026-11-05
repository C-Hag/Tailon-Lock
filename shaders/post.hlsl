// post.hlsl - the frame's last steps: a bright pass and a separable blur
// for bloom, then exposure, ACES tonemapping and gamma, and the software
// HUD composited on top (premultiplied alpha, nearest filtered).

[[vk::binding(0, 3)]] cbuffer PostF : register(b0, space3) {
  float4 P; // x: exposure, y: bloom strength, z/w: blur step (uv)
  float4 G; // film grade: warm tint, vignette, grain, time
  float4 Hz; // heat haze: band y0, y1 (uv), strength (uv), time
  float4 D;  // x: weight of the main picture over the second (dissolve), y: whip smear (uv)
};
[[vk::combinedImageSampler]][[vk::binding(0, 2)]] Texture2D Tex0 : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]] SamplerState Smp0 : register(s0, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]] Texture2D Tex1 : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]] SamplerState Smp1 : register(s1, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]] Texture2D Tex2 : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]] SamplerState Smp2 : register(s2, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]] Texture2D Tex3 : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]] SamplerState Smp3 : register(s3, space2);

struct VSOut {
  float4 clip : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID) {
  VSOut o;
  float2 p = float2((id << 1) & 2, id & 2);
  o.clip = float4(p * 2 - 1, 0, 1);
  o.uv = float2(p.x, 1 - p.y);
  return o;
}

float4 fs_bright(VSOut i) : SV_Target0 {
  float3 c = Tex0.Sample(Smp0, i.uv).rgb;
  float l = max(c.r, max(c.g, c.b));
  return float4(c * saturate((l - 1.0) / max(l, 1e-4)), 1);
}

float4 fs_blur(VSOut i) : SV_Target0 {
  const float w[5] = { 0.227, 0.194, 0.121, 0.054, 0.016 };
  float3 c = Tex0.Sample(Smp0, i.uv).rgb * w[0];
  [unroll] for (int k = 1; k < 5; k++) {
    c += Tex0.Sample(Smp0, i.uv + P.zw * k).rgb * w[k];
    c += Tex0.Sample(Smp0, i.uv - P.zw * k).rgb * w[k];
  }
  return float4(c, 1);
}

float3 aces(float3 x) {
  return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 fs_final(VSOut i) : SV_Target0 {
  float2 uv = i.uv;
  if (Hz.z > 0 && uv.y > Hz.x && uv.y < Hz.y) {
    // heat shimmer, stronger toward the bottom of the band
    float k = (uv.y - Hz.x) / max(Hz.y - Hz.x, 1e-3);
    uv.x += sin(uv.y * 480 + Hz.w * 23) * sin(uv.y * 70 - Hz.w * 7) * Hz.z * k;
  }
  float3 hdr = 0;
  // a whip pan smears the frame sideways, a motion blur over four steps of
  // the shift (the browser's five shifted copies, made continuous)
  int n = D.y != 0 ? 16 : 1;
  for (int k = 0; k < n; k++) {
    float2 q = uv + float2(D.y * 4.0 * k / 16.0, 0);
    float3 s = Tex0.Sample(Smp0, q).rgb + Tex1.Sample(Smp1, q).rgb * P.y;
    if (D.x < 1) s = lerp(Tex3.Sample(Smp3, q).rgb, s, D.x);
    hdr += s / n;
  }
  float3 c = pow(aces(hdr * P.x), 1 / 2.2);
  // a soft vignette, deeper with the film grade
  float2 d = i.uv - 0.5;
  c *= 1 - dot(d, d) * (0.35 + G.y * 2.2);
  // warm tint and grain
  c *= 1 + G.x * float3(0.5, 0.1, -0.4);
  // integer hash per pixel and frame (a sine hash bands at these magnitudes)
  uint2 q = uint2(i.clip.xy);
  uint h = q.x * 1973u + q.y * 9277u + uint(G.w * 60.0) * 26699u;
  h = (h ^ (h >> 15)) * 0x2c1b3c6du;
  h = (h ^ (h >> 12)) * 0x297a2d39u;
  h ^= h >> 15;
  float grain = float(h & 0xffffu) / 65535.0;
  c += (grain - 0.5) * G.z * 0.45;
  float4 ov = Tex2.Sample(Smp2, i.uv);
  return float4(ov.rgb + c * (1 - ov.a), 1);
}
