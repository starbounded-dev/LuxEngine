// SMAA 1x - pass 3 of 3: neighbourhood blending.
// Based on SMAA by Jorge Jimenez et al. (MIT), ported from Core/vendor/smaa/SMAA.hlsl.
//
// Compute rather than a fullscreen quad, matching the GTAO/SSR pattern.
//
// Takes the blending weights from pass 2 and resolves the final colour: each pixel is
// mixed with the neighbour its weight points at, which is what actually smooths the
// staircase. Bilinear filtering does the mixing for free - the weight is folded into
// the sample coordinate rather than applied afterwards.
#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

layout(set = 1, binding = 0, rgba8) restrict writeonly uniform image2D o_Output;
layout(set = 1, binding = 1) uniform texture2D u_InputTex;
layout(set = 1, binding = 2) uniform texture2D u_BlendTex;

layout(push_constant) uniform PushConstants
{
	vec4 SMAA_RT_METRICS; // (1/w, 1/h, w, h)
} u_Uniforms;

layout(local_size_x = 8, local_size_y = 8) in;

vec4 SampleLinearZero(texture2D tex, vec2 coord)
{
	return textureLod(sampler2D(tex, r_LinearSampler), coord, 0.0);
}

void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value)
{
	if (cond.x) variable.x = value.x;
	if (cond.y) variable.y = value.y;
}

void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value)
{
	SMAAMovc(cond.xy, variable.xy, value.xy);
	SMAAMovc(cond.zw, variable.zw, value.zw);
}

void main()
{
	const ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Uniforms.SMAA_RT_METRICS.zw);

	if (pixelCoord.x >= size.x || pixelCoord.y >= size.y)
		return;

	const vec2 texcoord = (vec2(pixelCoord) + 0.5) * u_Uniforms.SMAA_RT_METRICS.xy;

	const vec4 offset = fma(u_Uniforms.SMAA_RT_METRICS.xyxy, vec4(1.0, 0.0, 0.0, 1.0), texcoord.xyxy);

	// Gather the four weights that can affect this pixel.
	vec4 a;
	a.x  = SampleLinearZero(u_BlendTex, offset.xy).a;  // right
	a.y  = SampleLinearZero(u_BlendTex, offset.zw).g;  // top
	a.wz = SampleLinearZero(u_BlendTex, texcoord).xz;  // bottom / left

	// No weight anywhere means no edge touched this pixel: pass the colour through
	// rather than paying for a blend that would resolve to itself.
	if (dot(a, vec4(1.0)) < 1e-5)
	{
		imageStore(o_Output, pixelCoord, SampleLinearZero(u_InputTex, texcoord));
		return;
	}

	// Pick the dominant axis: true means this pixel blends sideways, not vertically.
	const bool h = max(a.x, a.z) > max(a.y, a.w);

	// Fold the weights into two signed offsets along the winning axis, then let
	// bilinear filtering perform the actual interpolation between the two texels.
	vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
	vec2 blendingWeight = a.yw;
	SMAAMovc(bvec4(h, h, h, h), blendingOffset, vec4(a.x, 0.0, a.z, 0.0));
	SMAAMovc(bvec2(h, h), blendingWeight, a.xz);
	blendingWeight /= dot(blendingWeight, vec2(1.0));

	const vec4 blendingCoord = fma(blendingOffset,
		vec4(u_Uniforms.SMAA_RT_METRICS.xy, -u_Uniforms.SMAA_RT_METRICS.xy), texcoord.xyxy);

	vec4 color = blendingWeight.x * SampleLinearZero(u_InputTex, blendingCoord.xy);
	color += blendingWeight.y * SampleLinearZero(u_InputTex, blendingCoord.zw);

	imageStore(o_Output, pixelCoord, color);
}
