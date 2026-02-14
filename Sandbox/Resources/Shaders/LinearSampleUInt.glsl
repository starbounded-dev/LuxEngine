#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

layout(binding = 0) uniform utexture2D u_InputTexture;

layout(binding = 1) uniform writeonly uimage2D o_OutputTexture;

layout(push_constant) uniform PushConstants
{
    vec2 TexelSize;
    int SourceMipLevel;
} u_PushConstants;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
    ivec2 outputCoord = ivec2(gl_GlobalInvocationID.xy);
    
    ivec2 outputSize = imageSize(o_OutputTexture);
    if (outputCoord.x >= outputSize.x || outputCoord.y >= outputSize.y)
        return;
    
    vec2 uv = (vec2(outputCoord) + 0.5) * u_PushConstants.TexelSize;
    
    uvec4 color = SampleLinearLOD(u_InputTexture, uv, float(u_PushConstants.SourceMipLevel));
    imageStore(o_OutputTexture, outputCoord, color);
}
