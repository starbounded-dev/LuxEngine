#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

layout(binding = 0, rgba16f) restrict writeonly uniform image2D o_Image;

const float Epsilon = 1.0e-4;

layout(binding = 1) uniform texture2D u_Texture;
layout(binding = 2) uniform texture2D u_BloomTexture;

layout(push_constant) uniform Uniforms
{
    vec4 Params; // (x) threshold, (y) threshold - knee, (z) knee * 2, (w) 0.25 / knee
    vec4 TexSize;
    float LOD;
    int Mode; // See defines below
} u_Uniforms;

#define MODE_PREFILTER      0
#define MODE_DOWNSAMPLE     1
#define MODE_UPSAMPLE_FIRST 2
#define MODE_UPSAMPLE       3

vec3 DownsampleBox13(texture2D tex, float lod, vec2 uv, vec2 texelSize)
{
    // Center
    vec3 A = SampleLinearLOD(tex, uv, lod).rgb;

    texelSize *= 0.5f; // Sample from center of texels

    // Inner box
    vec3 B = SampleLinearLOD(tex, uv + texelSize * vec2(-1.0f, -1.0f), lod).rgb;
    vec3 C = SampleLinearLOD(tex, uv + texelSize * vec2(-1.0f, 1.0f), lod).rgb;
    vec3 D = SampleLinearLOD(tex, uv + texelSize * vec2(1.0f, 1.0f), lod).rgb;
    vec3 E = SampleLinearLOD(tex, uv + texelSize * vec2(1.0f, -1.0f), lod).rgb;

    // Outer box
    vec3 F = SampleLinearLOD(tex, uv + texelSize * vec2(-2.0f, -2.0f), lod).rgb;
    vec3 G = SampleLinearLOD(tex, uv + texelSize * vec2(-2.0f, 0.0f), lod).rgb;
    vec3 H = SampleLinearLOD(tex, uv + texelSize * vec2(0.0f, 2.0f), lod).rgb;
    vec3 I = SampleLinearLOD(tex, uv + texelSize * vec2(2.0f, 2.0f), lod).rgb;
    vec3 J = SampleLinearLOD(tex, uv + texelSize * vec2(2.0f, 2.0f), lod).rgb;
    vec3 K = SampleLinearLOD(tex, uv + texelSize * vec2(2.0f, 0.0f), lod).rgb;
    vec3 L = SampleLinearLOD(tex, uv + texelSize * vec2(-2.0f, -2.0f), lod).rgb;
    vec3 M = SampleLinearLOD(tex, uv + texelSize * vec2(0.0f, -2.0f), lod).rgb;

    // Weights
    vec3 result = vec3(0.0);
    // Inner box
    result += (B + C + D + E) * 0.5f;
    // Bottom-left box
    result += (F + G + A + M) * 0.125f;
    // Top-left box
    result += (G + H + I + A) * 0.125f;
    // Top-right box
    result += (A + I + J + K) * 0.125f;
    // Bottom-right box
    result += (M + A + K + L) * 0.125f;

    // 4 samples each
    result *= 0.25f;

    return result;
}

// Quadratic color thresholding
// curve = (threshold - knee, knee * 2, 0.25 / knee)
vec4 QuadraticThreshold(vec4 color, float threshold, vec3 curve)
{
    // Maximum pixel brightness
    float brightness = max(max(color.r, color.g), color.b);
    // Quadratic curve
    float rq = clamp(brightness - curve.x, 0.0, curve.y);
    rq = (rq * rq) * curve.z;
    color *= max(rq, brightness - threshold) / max(brightness, Epsilon);
    return color;
}

vec4 Prefilter(vec4 color, vec2 uv)
{
    float clampValue = 20.0f;
    color = clamp(color, vec4(0.0f), vec4(clampValue));
    color = QuadraticThreshold(color, u_Uniforms.Params.x, u_Uniforms.Params.yzw);
    return color;
}

vec3 UpsampleTent9(texture2D tex, float lod, vec2 uv, vec2 texelSize, float radius)
{
    vec4 offset = texelSize.xyxy * vec4(1.0f, 1.0f, -1.0f, 0.0f) * radius;

    // Center
    vec3 result = SampleLinearLOD(tex, uv, lod).rgb * 4.0f;

    result += SampleLinearLOD(tex, uv - offset.xy, lod).rgb;
    result += SampleLinearLOD(tex, uv - offset.wy, lod).rgb * 2.0;
    result += SampleLinearLOD(tex, uv - offset.zy, lod).rgb;

    result += SampleLinearLOD(tex, uv + offset.zw, lod).rgb * 2.0;
    result += SampleLinearLOD(tex, uv + offset.xw, lod).rgb * 2.0;

    result += SampleLinearLOD(tex, uv + offset.zy, lod).rgb;
    result += SampleLinearLOD(tex, uv + offset.wy, lod).rgb * 2.0;
    result += SampleLinearLOD(tex, uv + offset.xy, lod).rgb;

    return result * (1.0f / 16.0f);
}

layout(local_size_x = 4, local_size_y = 4) in;
void main()
{
    vec2 imgSize = vec2(imageSize(o_Image));

    ivec2 invocID = ivec2(gl_GlobalInvocationID);
    vec2 texCoords = vec2(float(invocID.x) / imgSize.x, float(invocID.y) / imgSize.y);
    texCoords += (1.0f / imgSize) * 0.5f;

    vec2 texSize = vec2(GetTextureSize(u_Texture, int(u_Uniforms.LOD)));
    vec4 color = vec4(1, 0, 1, 1);
    if (u_Uniforms.Mode == MODE_PREFILTER)
    {
        color.rgb = DownsampleBox13(u_Texture, 0, texCoords, 1.0f / texSize);
        color = Prefilter(color, texCoords);
        color.a = 1.0f;
    }
    else if (u_Uniforms.Mode == MODE_UPSAMPLE_FIRST)
    {
        vec2 bloomTexSize = u_Uniforms.TexSize.xy;
        float sampleScale = 1.0f;
        vec3 upsampledTexture = UpsampleTent9(u_Texture, u_Uniforms.LOD + 1.0f, texCoords, 1.0f / bloomTexSize, sampleScale);

        vec3 existing = SampleLinearLOD(u_Texture, texCoords, u_Uniforms.LOD).rgb;
        color.rgb = existing + upsampledTexture;
    }
    else if (u_Uniforms.Mode == MODE_UPSAMPLE)
    {
        vec2 bloomTexSize = u_Uniforms.TexSize.xy;
        float sampleScale = 1.0f;
        vec3 upsampledTexture = UpsampleTent9(u_BloomTexture, 0.0f, texCoords, 1.0f / bloomTexSize, sampleScale);

        vec3 existing = SampleLinearLOD(u_Texture, texCoords, u_Uniforms.LOD).rgb;
        color.rgb = existing + upsampledTexture;
    }
    else if (u_Uniforms.Mode == MODE_DOWNSAMPLE)
    {
        // Downsample
        color.rgb = DownsampleBox13(u_Texture, u_Uniforms.LOD, texCoords, 1.0f / texSize);
    }
    
    imageStore(o_Image, ivec2(gl_GlobalInvocationID), color);
}
