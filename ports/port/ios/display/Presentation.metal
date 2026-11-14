// Original single-pass display effects for this native port.
// Preset names describe the visual families also offered by the 3SX reference.
// These are lightweight approximations, not the upstream libretro shaders.
#include <metal_stdlib>
using namespace metal;

struct PresentationUniforms {
    float4 controls;
    float4 source;
    float4 output;
    float4 rect;
};

struct PresentationVertex {
    float4 position [[position]];
    float2 uv;
};

constant float kPi = 3.14159265358979323846f;
constexpr sampler bilinear(coord::normalized, address::clamp_to_edge, filter::linear);

vertex PresentationVertex mvc2_present_vertex(uint index [[vertex_id]]) {
    const float2 positions[] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    const float2 coordinates[] = {float2(0, 1), float2(2, 1), float2(0, -1)};
    PresentationVertex result;
    result.position = float4(positions[index], 0, 1);
    result.uv = coordinates[index];
    return result;
}

float3 fetch(texture2d<float> image, float2 uv) {
    return image.sample(bilinear, uv).rgb;
}

float3 pixel(texture2d<float> image, int2 coordinate) {
    int2 size = int2(image.get_width(), image.get_height());
    return image.read(uint2(clamp(coordinate, int2(0), size - 1))).rgb;
}

float2 crisp_uv(float2 uv, float2 size) {
    float2 position = uv * size;
    float2 center = floor(position) + 0.5f;
    float2 delta = position - center;
    // Smooth subpixel transitions, retaining a broad pixel center.
    float2 transition = delta * abs(delta) * 2.0f;
    return (center + transition) / size;
}

float beam(float y, float darkness) {
    float phase = cos(y * 2.0f * kPi);
    return 1.0f - darkness * (0.5f + 0.5f * phase);
}

float3 rgb_mask(float x, float strength) {
    int channel = int(floor(x)) % 3;
    float3 mask(1.0f - strength);
    if (channel == 0) mask.r = 1;
    else if (channel == 1) mask.g = 1;
    else mask.b = 1;
    return mask;
}

float2 curve(float2 uv, float2 curvature) {
    float2 centered = uv * 2.0f - 1.0f;
    centered *= 1.0f + centered.yx * centered.yx * curvature;
    return centered * 0.5f + 0.5f;
}

bool outside(float2 uv) {
    return any(uv < float2(0)) || any(uv > float2(1));
}

float3 gaussian_reconstruction(texture2d<float> image, float2 uv, float2 size) {
    float2 location = uv * size - 0.5f;
    int2 center = int2(floor(location));
    float2 fraction = fract(location);
    float3 sum(0);
    float total = 0;
    for (int row = -1; row <= 2; ++row) {
        for (int column = -1; column <= 2; ++column) {
            float2 distance = float2(column, row) - fraction;
            float weight = exp(-dot(distance, distance) * 2.6f);
            sum += pixel(image, center + int2(column, row)) * weight;
            total += weight;
        }
    }
    return sum / max(total, 0.0001f);
}

float3 to_yiq(float3 rgb) {
    return float3(dot(rgb, float3(0.299f, 0.587f, 0.114f)),
                  dot(rgb, float3(0.596f, -0.274f, -0.322f)),
                  dot(rgb, float3(0.211f, -0.523f, 0.312f)));
}

float3 from_yiq(float3 yiq) {
    return float3(yiq.x + 0.956f * yiq.y + 0.621f * yiq.z,
                  yiq.x - 0.272f * yiq.y - 0.647f * yiq.z,
                  yiq.x - 1.106f * yiq.y + 1.703f * yiq.z);
}

float3 composite(texture2d<float> image, float2 uv, float2 size) {
    float2 texel = 1.0f / size;
    float3 center = to_yiq(fetch(image, uv));
    float2 chroma(0);
    float total = 0;
    for (int column = -3; column <= 3; ++column) {
        float weight = 4.0f - abs(float(column));
        chroma += to_yiq(fetch(image, uv + float2(column * texel.x, 0))).yz * weight;
        total += weight;
    }
    center.yz = chroma / total;
    // Fixed spatial noise makes the preview deterministic and inexpensive.
    float noise = fract(sin(dot(floor(uv * size), float2(17.13f, 91.77f))) * 4381.17f);
    center.x += (noise - 0.5f) * 0.015f;
    return from_yiq(center) * beam(uv.y * size.y, 0.10f);
}

float3 preset_color(texture2d<float> image, float2 uv, float2 size,
                    float2 output_pixel, int mode, float strength) {
    if (mode == 1) return fetch(image, crisp_uv(uv, size));
    if (mode == 2) {
        float3 color = fetch(image, crisp_uv(uv, size));
        float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        return color * beam(uv.y * size.y, 0.18f + 0.20f * (1 - luminance));
    }
    if (mode == 3) {
        float2 warped = curve(uv, float2(0.035f, 0.05f) * strength);
        if (outside(warped)) return float3(0);
        return gaussian_reconstruction(image, warped, size) *
               beam(warped.y * size.y, 0.25f) * rgb_mask(output_pixel.x, 0.22f) * 1.12f;
    }
    if (mode == 4) {
        float3 color = fetch(image, crisp_uv(uv, size));
        float2 spot = fract(output_pixel / float2(3, 3)) - 0.5f;
        float glow = 0.6f + 0.4f * exp(-dot(spot, spot) * 8.0f);
        return color * glow * rgb_mask(output_pixel.x + floor(output_pixel.y / 3), 0.3f);
    }
    if (mode == 5) {
        float3 color = fetch(image, uv);
        int row = int(floor(output_pixel.y)) % 4;
        float stagger = (int(floor(output_pixel.y / 4)) & 1) ? 1.5f : 0;
        float gap = row == 3 ? 0.65f : 1.0f;
        return color * rgb_mask(output_pixel.x + stagger, 0.28f) * gap *
               beam(uv.y * size.y, 0.15f) * 1.12f;
    }
    if (mode == 6) return composite(image, uv, size);
    if (mode == 7) {
        float2 texel = 1.0f / size;
        float3 color = fetch(image, crisp_uv(uv, size));
        float3 halo = fetch(image, uv + float2(texel.x, 0)) +
                      fetch(image, uv - float2(texel.x, 0));
        color = color * 0.85f + halo * 0.10f;
        return color * rgb_mask(output_pixel.x, 0.30f) * beam(uv.y * size.y, 0.13f) * 1.12f;
    }
    if (mode == 8) {
        float2 warped = curve(uv, float2(0.12f, 0.16f) * strength);
        if (outside(warped)) return float3(0);
        float2 edge = abs(warped * 2 - 1);
        // Rounded glass corners, with a gentle falloff toward the bezel.
        float2 corner = max(edge - float2(0.90f), 0.0f);
        float corner_fade = 1.0f - smoothstep(0.075f, 0.12f, length(corner));
        float vignette = 1.0f - 0.18f * dot(edge, edge);
        return fetch(image, warped) * beam(warped.y * size.y, 0.29f) *
               corner_fade * vignette * 1.12f;
    }
    if (mode == 9) {
        float2 texel = 1.0f / size;
        float3 center = fetch(image, crisp_uv(uv, size));
        float3 neighbors = fetch(image, uv + float2(texel.x, 0)) +
                           fetch(image, uv - float2(texel.x, 0)) +
                           fetch(image, uv + float2(0, texel.y)) +
                           fetch(image, uv - float2(0, texel.y));
        float3 sharpened = center * 1.4f - neighbors * 0.1f;
        return sharpened * beam(uv.y * size.y, 0.17f);
    }
    return fetch(image, uv);
}

fragment float4 mvc2_present_fragment(PresentationVertex in [[stage_in]],
                                      texture2d<float> image [[texture(0)]],
                                      constant PresentationUniforms& options [[buffer(0)]]) {
    if (any(options.rect.zw <= float2(0))) return float4(0, 0, 0, 1);
    float2 output_pixel = in.uv * options.output.xy;
    float2 uv = (output_pixel - options.rect.xy) / options.rect.zw;
    if (outside(uv)) return float4(0, 0, 0, 1);
    float3 original = fetch(image, uv);
    int mode = int(options.controls.y);
    float strength = clamp(options.controls.x, 0.0f, 1.0f);
    if (mode == 0 || strength <= 0) return float4(original, 1);
    float3 effect = preset_color(image, uv, options.source.xy, output_pixel, mode, strength);
    return float4(clamp(mix(original, effect, strength), 0.0f, 1.0f), 1);
}
