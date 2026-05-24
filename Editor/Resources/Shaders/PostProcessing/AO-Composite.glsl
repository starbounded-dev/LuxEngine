#version 430 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 o_TexCoord;
void main()
{
    o_TexCoord = a_TexCoord;
    gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core
#pragma stage : frag

#include <Buffers.glslh>
#include <GTAO.slh>
#include <Samplers.glslh>

#define ENABLED_GTAO (__HZ_AO_METHOD & HZ_AO_METHOD_GTAO)
#define ENABLED_HBAO (__HZ_AO_METHOD & HZ_AO_METHOD_HBAO)

#if ENABLED_GTAO
layout(set = 1, binding = 0) uniform utexture2D u_GTAOTex;
layout(set = 1, binding = 1) uniform texture2D u_Depth;
layout(set = 1, binding = 2) uniform texture2D u_Normal;
#endif

#if ENABLED_HBAO
layout(set = 1, binding = 3) uniform texture2D u_HBAOTex;
#endif

layout(location = 0) out vec4 o_Occlusion;
layout(location = 0) in vec2 vs_TexCoord;

#if ENABLED_GTAO
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

float DecodeGTAO(uint packedValue)
{
    #if __HZ_GTAO_COMPUTE_BENT_NORMALS
        return float(packedValue >> 24u) / 255.0;
    #else
        return float(packedValue) / 255.0;
    #endif
}

float FetchGTAO(ivec2 texel)
{
    ivec2 aoSize = textureSize(usampler2D(u_GTAOTex, r_PointSampler), 0);
    texel = clamp(texel, ivec2(0), max(aoSize - ivec2(1), ivec2(0)));
    return DecodeGTAO(texelFetch(usampler2D(u_GTAOTex, r_PointSampler), texel, 0).x);
}

float UpscaleGTAO(vec2 uv)
{
    ivec2 aoSize = textureSize(usampler2D(u_GTAOTex, r_PointSampler), 0);
    ivec2 depthSize = textureSize(sampler2D(u_Depth, r_PointSampler), 0);
    if (aoSize.x >= depthSize.x && aoSize.y >= depthSize.y)
        return FetchGTAO(ivec2(clamp(uv * vec2(aoSize), vec2(0.0), vec2(aoSize - ivec2(1)))));

    vec2 aoTexel = uv * vec2(aoSize) - vec2(0.5);
    ivec2 baseTexel = ivec2(floor(aoTexel));
    float centerDepth = ReadDepth(uv);
    vec3 centerNormal = ReadNormal(uv);

    float weightedAO = 0.0;
    float totalWeight = 0.0;

    for (int y = -1; y <= 2; y++)
    {
        for (int x = -1; x <= 2; x++)
        {
            ivec2 sampleTexel = baseTexel + ivec2(x, y);
            ivec2 clampedTexel = clamp(sampleTexel, ivec2(0), max(aoSize - ivec2(1), ivec2(0)));
            vec2 sampleUV = (vec2(clampedTexel) + vec2(0.5)) / vec2(aoSize);

            float sampleDepth = ReadDepth(sampleUV);
            vec3 sampleNormal = ReadNormal(sampleUV);
            vec2 spatialOffset = (vec2(sampleTexel) + vec2(0.5)) - aoTexel;

            float relativeDepthDelta = abs(sampleDepth - centerDepth) / max(abs(centerDepth), 1.0);
            float depthWeight = exp(-relativeDepthDelta / 0.035);
            float normalWeight = pow(clamp(dot(centerNormal, sampleNormal), 0.0, 1.0), 24.0);
            float spatialWeight = exp(-dot(spatialOffset, spatialOffset) * 0.55);
            float weight = depthWeight * normalWeight * spatialWeight;

            weightedAO += FetchGTAO(clampedTexel) * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= 0.0001)
        return FetchGTAO(ivec2(round(aoTexel)));

    return weightedAO / totalWeight;
}
#endif

void main()
{
    float occlusion = 1.0f;
#if ENABLED_GTAO
    float ao = UpscaleGTAO(vs_TexCoord);
    occlusion = min(ao * XE_GTAO_OCCLUSION_TERM_SCALE, 1.0f);
#endif
#if ENABLED_HBAO
    occlusion *= SampleLinear(u_HBAOTex, vs_TexCoord).x;
#endif

    o_Occlusion = occlusion.xxxx;
}

