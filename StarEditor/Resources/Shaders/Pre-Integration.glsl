#version 430 core
#pragma stage : comp

#include <Samplers.glslh>

layout(push_constant) uniform Info
{
	vec2 u_HZBInvRes;
	vec2 u_InvRes;
	vec2 u_ProjectionParams; //(x) = Near plane, (y) = Far plane // Reversed
    int u_PrevMip;
};

layout(binding = 0, r8) restrict uniform writeonly image2D o_VisibilityImage;
layout(binding = 1) uniform texture2D u_VisibilityTex;
layout(binding = 2) uniform texture2D u_HZB;

float LinearizeDepth(float d)
{
	return u_ProjectionParams.x * u_ProjectionParams.y / (u_ProjectionParams.y + d * (u_ProjectionParams.x - u_ProjectionParams.y));
}

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    ivec2 base = ivec2(gl_GlobalInvocationID);
	vec2 hzbUV = u_HZBInvRes * base;
	vec2 uv = u_InvRes * base;
    
	vec4 fineZ;
	fineZ.x = LinearizeDepth(SampleLinearLOD(u_HZB, hzbUV + u_HZBInvRes * vec2(-0.5, -0.5), u_PrevMip).x);
	fineZ.y = LinearizeDepth(SampleLinearLOD(u_HZB, hzbUV + u_HZBInvRes * vec2(-0.5,  0.0), u_PrevMip).x);
	fineZ.z = LinearizeDepth(SampleLinearLOD(u_HZB, hzbUV + u_HZBInvRes * vec2( 0.0, -0.5), u_PrevMip).x);
	fineZ.w = LinearizeDepth(SampleLinearLOD(u_HZB, hzbUV + u_HZBInvRes * vec2( 0.5,  0.5), u_PrevMip).x);

	/* Fetch fine visibility from previous visibility map LOD */
	vec4 visibility;
	visibility.x = SampleLinearLOD(u_VisibilityTex, uv + u_InvRes * vec2(-0.5, -0.5), u_PrevMip).r;
    visibility.y = SampleLinearLOD(u_VisibilityTex, uv + u_InvRes * vec2(-0.5,  0.0), u_PrevMip).r;
    visibility.z = SampleLinearLOD(u_VisibilityTex, uv + u_InvRes * vec2( 0.0, -0.5), u_PrevMip).r;
    visibility.w = SampleLinearLOD(u_VisibilityTex, uv + u_InvRes * vec2( 0.5,  0.5), u_PrevMip).r;

	/* Integrate visibility */
	float maxZ = max(max(fineZ.x, fineZ.y), max(fineZ.z, fineZ.w));
	vec4 integration = (fineZ / maxZ) * visibility;

	/* Compute coarse visibility (with SIMD 'dot' intrinsic) */
	float coarseVisibility = dot(vec4(0.25), integration);

	imageStore(o_VisibilityImage, base, coarseVisibility.xxxx);
}