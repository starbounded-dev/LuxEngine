// Mip downsample for sRGB colour textures.
//
// sRGB images cannot be storage images, so the chain is built in an RGBA8 UNORM image holding the
// same sRGB-encoded bytes. Averaging those bytes directly darkens every mip (a black and white
// checker would come out at 0.5 encoded, 0.21 linear). Instead each source texel is decoded to linear,
// the 2x2 block is averaged, and the result is encoded again. Alpha is linear and averaged as is.

#version 450 core
#pragma stage : comp

#include <Common.glslh>
#include <Samplers.glslh>

layout(binding = 0) uniform texture2D u_InputTexture;

layout(binding = 1) uniform writeonly image2D o_OutputTexture;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
    ivec2 outputCoord = ivec2(gl_GlobalInvocationID.xy);

    ivec2 outputSize = imageSize(o_OutputTexture);
    if (outputCoord.x >= outputSize.x || outputCoord.y >= outputSize.y)
        return;

    // The input view holds only the source mip, so it is fetched at level 0. Odd source edges clamp.
    ivec2 inputMax = textureSize(sampler2D(u_InputTexture, r_PointSampler), 0) - 1;
    ivec2 base = outputCoord * 2;

    vec4 sum = vec4(0.0);
    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 2; x++)
        {
            vec4 encoded = texelFetch(sampler2D(u_InputTexture, r_PointSampler), min(base + ivec2(x, y), inputMax), 0);
            sum += vec4(ToLinear(encoded).rgb, encoded.a);
        }
    }
    vec4 average = sum * 0.25;

    vec3 encoded = vec3(Convert_sRGB_FromLinear(average.r), Convert_sRGB_FromLinear(average.g), Convert_sRGB_FromLinear(average.b));
    imageStore(o_OutputTexture, outputCoord, vec4(encoded, average.a));
}
