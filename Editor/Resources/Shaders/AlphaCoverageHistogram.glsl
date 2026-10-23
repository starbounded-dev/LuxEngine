// Counts one mip's alpha into 256 bins, for coverage-preserving mips (Castano, "Computing Alpha
// Mipmaps"): Texture2D::GenerateMips with an alpha cutoff histograms every level of the box-filtered
// chain, then AlphaCoverageScale.glsl rescales each level's alpha from the histograms. 8-bit alpha
// makes the 256 bins exact. Each workgroup counts in shared memory and merges with one atomicAdd per
// bin, as LuminanceHistogram.glsl does.

#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

#define ALPHA_COVERAGE_BINS 256
#define ALPHA_COVERAGE_MAX_MIPS 16

// One mip of the chain, a single-level view.
layout(binding = 0) uniform texture2D u_InputTexture;

// Storage-buffer (set, binding) slots are a global namespace (Rendering.md, Invariant 1): set 1,
// binding 26 is this histogram's, shared with AlphaCoverageScale.glsl. Mip m's bins start at m * 256.
layout(std430, set = 1, binding = 26) buffer AlphaCoverageHistogram
{
	uint Bins[ALPHA_COVERAGE_MAX_MIPS * ALPHA_COVERAGE_BINS];
} b_Histogram;

layout(push_constant) uniform PushConstants
{
	uint Mip;
} u_PushConstants;

shared uint s_Bins[ALPHA_COVERAGE_BINS];

// 256 threads: one bin each to clear and merge.
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
void main()
{
	const uint localIndex = gl_LocalInvocationIndex;
	s_Bins[localIndex] = 0u;
	barrier();

	const ivec2 size = textureSize(sampler2D(u_InputTexture, r_PointSampler), 0);
	const ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	if (coord.x < size.x && coord.y < size.y)
	{
		const float alpha = texelFetch(sampler2D(u_InputTexture, r_PointSampler), coord, 0).a;
		atomicAdd(s_Bins[uint(round(clamp(alpha, 0.0, 1.0) * 255.0))], 1u);
	}
	barrier();

	if (s_Bins[localIndex] != 0u)
		atomicAdd(b_Histogram.Bins[u_PushConstants.Mip * ALPHA_COVERAGE_BINS + localIndex], s_Bins[localIndex]);
}
