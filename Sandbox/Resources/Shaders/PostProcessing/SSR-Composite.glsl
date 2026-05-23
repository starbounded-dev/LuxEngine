#version 430 core
#pragma stage:vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 vs_TexCoords;

void main()
{
    vs_TexCoords = a_TexCoord;
    gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core
#pragma stage:frag

#include <Buffers.glslh>
#include <Samplers.glslh>

layout(set = 1, binding = 0) uniform texture2D u_SSR;
layout(set = 1, binding = 1) uniform texture2D u_Depth;
layout(set = 1, binding = 2) uniform texture2D u_Normal;

layout(push_constant) uniform Uniforms
{
    uint ResolutionScale;
    uint BilateralUpscale;
    uint QuarterDebug;
    float DepthSigma;
    float NormalSigma;
} u_Uniforms;

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 vs_TexCoords;

float LinearizeDepth(float screenDepth)
{
    float depthLinearizeMul = u_Camera.DepthUnpackConsts.x;
    float depthLinearizeAdd = u_Camera.DepthUnpackConsts.y;
    return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float ReadDepth(vec2 uv)
{
    return LinearizeDepth(texture(sampler2D(u_Depth, r_PointSampler), uv).r);
}

vec3 ReadNormal(vec2 uv)
{
    vec3 normal = texture(sampler2D(u_Normal, r_PointSampler), uv).xyz;
    float normalLength = length(normal);
    if (normalLength < 0.0001)
        return vec3(0.0, 0.0, 1.0);

    return normal / normalLength;
}

vec4 BilateralUpscaleSSR(vec2 uv)
{
    ivec2 ssrSize = GetTextureSize(u_SSR, 0);
    vec2 ssrTexelSize = 1.0 / vec2(max(ssrSize, ivec2(1)));

    float centerDepth = ReadDepth(uv);
    vec3 centerNormal = ReadNormal(uv);

    vec4 centerSSR = SampleLinear(u_SSR, uv);
    vec4 weightedSSR = vec4(0.0);
    float totalWeight = 0.0;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 offset = vec2(x, y);
            vec2 sampleUV = clamp(uv + offset * ssrTexelSize, vec2(0.0), vec2(1.0));
            vec4 sampleSSR = SampleLinear(u_SSR, sampleUV);

            float sampleDepth = ReadDepth(sampleUV);
            vec3 sampleNormal = ReadNormal(sampleUV);

            float relativeDepthDelta = abs(sampleDepth - centerDepth) / max(abs(centerDepth), 1.0);
            float depthWeight = exp(-relativeDepthDelta / max(u_Uniforms.DepthSigma, 0.0001));
            float normalWeight = pow(clamp(dot(centerNormal, sampleNormal), 0.0, 1.0), max(u_Uniforms.NormalSigma, 1.0));
            float spatialWeight = exp(-dot(offset, offset) * 0.55);
            float confidenceWeight = max(sampleSSR.a, 0.001);
            float weight = depthWeight * normalWeight * spatialWeight * confidenceWeight;

            weightedSSR += sampleSSR * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= 0.0001)
        return centerSSR;

    vec4 result = weightedSSR / totalWeight;
    result.a = clamp(result.a, 0.0, 1.0);
    return result;
}

void main()
{
    bool useBilateralUpscale = u_Uniforms.BilateralUpscale != 0u || u_Uniforms.QuarterDebug != 0u;
    if (u_Uniforms.ResolutionScale <= 1u || !useBilateralUpscale)
    {
        outColor = SampleLinear(u_SSR, vs_TexCoords);
        return;
    }

    outColor = BilateralUpscaleSSR(vs_TexCoords);
}
