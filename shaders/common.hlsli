// common.hlsli - shared by the GPU renderer's shaders. Resource registers
// follow our GPU device's slots (src/platform/gx.h: vertex uniforms space1;
// fragment textures space2, uniforms space3); [[vk::]] attributes give the
// same layout to the SPIR-V build.

struct ViewData {
  row_major float4x4 viewProj;
  row_major float4x4 shadowVP;
  float4 camPos;   // xyz, w: time in seconds
  float4 camRight; // xyz, w: 1 / horizontal projection scale
  float4 camUp;    // xyz, w: 1 / vertical projection scale
  float4 camFwd;   // xyz, w: focal length in pixels of the viewport height
  float4 sunDir;   // toward the sun; w: sun intensity
  float4 sunCol;   // rgb; w: 1 when the shadow map is live
  float4 ambCol;   // rgb; w: ambient intensity
  float4 skyTop;   // rgb; w: star brightness
  float4 skyHor;   // rgb; w: water level
  float4 fogCol;   // rgb; w: fog density per metre
  float4 misc;     // x: fog start, y: classic arena, z: shadow texel size, w: the sea: 3 the opening film, 2 ULTRA, else 1
  float4 haze;     // rgb: the ground beyond what is drawn; w: the most a surface fades into the fog
};

float3 to_linear(float3 c) { return c * c; }

// The sky seen along a direction: horizon to zenith, a soft glow round the
// sun and its disc. Used by the sky pass and by water reflections.
float3 sky_color(ViewData v, float3 d, bool disc) {
  float h = saturate(d.y);
  float3 top = to_linear(v.skyTop.rgb), hor = to_linear(v.skyHor.rgb);
  float3 c = lerp(hor, top, pow(h, 0.45));
  // below the horizon: the far ground's haze, from where surfaces stop fading (so the sky ends at the level line)
  // (haze.w < 0: the hangar's studio backdrop, which darkens from the horizon colour below the eye line)
  if (d.y < 0) c = v.haze.w < 0 ? lerp(hor, to_linear(v.fogCol.rgb) * 0.8, saturate(-d.y * 4))
                             : lerp(to_linear(v.haze.rgb), to_linear(v.fogCol.rgb), v.haze.w);
  float s = saturate(dot(d, normalize(v.sunDir.xyz)));
  float3 sun = to_linear(v.sunCol.rgb) * v.sunDir.w;
  c += sun * (pow(s, 6) * 0.25 + pow(s, 64) * 0.6);
  if (disc && s > 0.9996) c += sun * 40;
  return c;
}

// Height fog that thickens toward the ground and takes the sun's colour
// when looking toward it.
float3 apply_fog(ViewData v, float3 c, float3 wp) {
  float3 d = wp - v.camPos.xyz;
  float dist = length(d);
  float3 dir = d / max(dist, 1e-3);
  float hf = exp(-max(0, min(wp.y, v.camPos.y) - v.skyHor.w) / 1800.0);
  float f = 1 - exp(-max(0, dist - v.misc.x) * v.fogCol.w * (0.35 + 0.65 * hf));
  float s = pow(saturate(dot(dir, normalize(v.sunDir.xyz))), 8);
  float3 fc = lerp(to_linear(v.fogCol.rgb), to_linear(v.sunCol.rgb) * v.sunDir.w, s * 0.5);
  return lerp(c, fc, v.haze.w < 0 ? saturate(f) : min(saturate(f), v.haze.w));
}

float hash21(float2 p) {
  p = frac(p * float2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return frac(p.x * p.y);
}

float vnoise(float2 p) {
  float2 i = floor(p), f = frac(p);
  float2 u = f * f * (3 - 2 * f);
  return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), u.x), lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), u.x), u.y);
}
