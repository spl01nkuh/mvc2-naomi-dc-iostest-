/*
 * rd_gpu_resolve.hlsl  The frame drawn larger than the window, filtered down to it: each pixel the
 * average, in linear light, of the frame's pixels under it. port/tools/gpu_shaders.sh builds it.
 */

#ifdef __spirv__
#define BIND(n) [[vk::combinedImageSampler]] [[vk::binding(n, 2)]]
#else
#define BIND(n)
#endif
BIND(0) Texture2D<float4> frame : register(t0, space2);
BIND(0) SamplerState s0 : register(s0, space2);

cbuffer Resolve : register(b0, space3) {
    float2 ratio;       /* the frame's pixels across and down a pixel here */
    float2 pad;
};

/* one triangle over the target */
float4 vs_full(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}

float3 to_linear(float3 c) {
    float3 low = step(c, 0.04045);
    return low * (c / 12.92) + (1.0 - low) * pow((max(c, 0.0) + 0.055) / 1.055, 2.4);
}

float3 to_srgb(float3 c) {
    float3 low = step(c, 0.0031308);
    return low * (c * 12.92) + (1.0 - low) * (1.055 * pow(max(c, 0.0031308), 1.0 / 2.4) - 0.055);
}

float4 ps_resolve(float4 pos : SV_Position) : SV_Target0 {
    float2 lo = (pos.xy - 0.5) * ratio, hi = lo + ratio;
    int2 a = (int2)floor(lo), b = (int2)ceil(hi);
    float3 sum = 0;
    [loop] for (int y = a.y; y < b.y; y++) {
        float wy = min(hi.y, y + 1.0) - max(lo.y, (float)y);
        [loop] for (int x = a.x; x < b.x; x++) {
            float wx = min(hi.x, x + 1.0) - max(lo.x, (float)x);
            sum += to_linear(frame.Load(int3(x, y, 0)).rgb) * (wx * wy);
        }
    }
    return float4(to_srgb(sum / (ratio.x * ratio.y)), 1.0);
}
