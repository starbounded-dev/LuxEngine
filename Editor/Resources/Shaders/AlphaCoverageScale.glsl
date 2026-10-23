// Writes one mip of a coverage-preserving chain (Castano, "Computing Alpha Mipmaps"): the
// box-filtered mip with its alpha rescaled so the share of texels at or above the material's cutoff
// matches mip 0's. Box averaging lowers alpha around every hole, so without this an alpha-tested
// texture loses coverage with each level and foliage thins out with distance.
//
// The new cutoff is solved from the histograms AlphaCoverageHistogram.glsl wrote: the bin whose
// coverage is closest to mip 0's. Every mip was downsampled from the unscaled level above, so the
// scaling never compounds. Mip 0 passes through unchanged.

#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

#define ALPHA_COVERAGE_BINS 256
#define ALPHA_COVERAGE_MAX_MIPS 16

// The box-filtered mip, a single-level view, and the same level of the output chain.
layout(binding = 0) uniform texture2D u_InputTexture;
layout(binding = 1) uniform writeonly image2D o_OutputTexture;

// Shared with AlphaCoverageHistogram.glsl at set 1, binding 26.
layout(std430, set = 1, binding = 26) readonly buffer AlphaCoverageHistogram
{
	uint Bins[ALPHA_COVERAGE_MAX_MIPS * ALPHA_COVERAGE_BINS];
} b_Histogram;

layout(push_constant) uniform PushConstants
{
	uint Mip;
	// The material's cutoff: alpha at or above it passes the alpha test.
	float AlphaCutoff;
} u_PushConstants;

shared float s_AlphaScale;

// The share of `mip`'s texels whose alpha bin is `firstBin` or higher.
float Coverage(uint mip, uint firstBin)
{
	uint covered = 0u;
	uint total = 0u;
	for (uint bin = 0u; bin < ALPHA_COVERAGE_BINS; bin++)
	{
		const uint count = b_Histogram.Bins[mip * ALPHA_COVERAGE_BINS + bin];
		total += count;
		covered += bin >= firstBin ? count : 0u;
	}
	return total > 0u ? float(covered) / float(total) : 0.0;
}

// The alpha scale that gives `mip` mip 0's coverage at the cutoff.
float SolveAlphaScale(uint mip, float alphaCutoff)
{
	const uint cutoffBin = clamp(uint(ceil(alphaCutoff * 255.0)), 1u, 255u);
	const float targetCoverage = Coverage(0u, cutoffBin);
	if (targetCoverage <= 0.0)
		return 1.0;

	uint total = 0u;
	for (uint bin = 0u; bin < ALPHA_COVERAGE_BINS; bin++)
		total += b_Histogram.Bins[mip * ALPHA_COVERAGE_BINS + bin];
	if (total == 0u)
		return 1.0;

	// Walk the bins down from opaque, accumulating coverage, and keep the bin closest to the target.
	// Ties keep the higher bin, the smaller scale.
	uint bestBin = cutoffBin;
	float bestError = 2.0;
	uint covered = 0u;
	for (uint bin = ALPHA_COVERAGE_BINS - 1u; bin >= 1u; bin--)
	{
		covered += b_Histogram.Bins[mip * ALPHA_COVERAGE_BINS + bin];
		const float error = abs(float(covered) / float(total) - targetCoverage);
		if (error < bestError)
		{
			bestError = error;
			bestBin = bin;
		}
	}

	return alphaCutoff / (float(bestBin) / 255.0);
}

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
	if (gl_LocalInvocationIndex == 0u)
		s_AlphaScale = u_PushConstants.Mip == 0u ? 1.0 : SolveAlphaScale(u_PushConstants.Mip, u_PushConstants.AlphaCutoff);
	barrier();

	const ivec2 outputCoord = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 outputSize = imageSize(o_OutputTexture);
	if (outputCoord.x >= outputSize.x || outputCoord.y >= outputSize.y)
		return;

	vec4 texel = texelFetch(sampler2D(u_InputTexture, r_PointSampler), outputCoord, 0);
	texel.a = clamp(texel.a * s_AlphaScale, 0.0, 1.0);
	imageStore(o_OutputTexture, outputCoord, texel);
}
