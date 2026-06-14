// Builds a 256-bin log-luminance histogram of the HDR scene color.
// Classic group-shared accumulation (Tardif / Bruneton): each workgroup
// accumulates into shared memory, then merges into the global histogram with
// a single atomicAdd per bin. Consumed by LuminanceAverage.glsl.
#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

#define HISTOGRAM_BINS 256
#define LUM_EPSILON 0.005

layout(set = 1, binding = 0) uniform texture2D u_SceneColor;

// NOTE: storage-buffer (set,binding) slots are a GLOBAL namespace in this engine
// (VulkanShaderCompiler dedupes via static s_StorageBuffers keyed by set+binding),
// so each distinct buffer must use a binding no other shader reuses. set=1 bindings
// 0-4 = mesh culling, 9-10 = light culling; 5 = luminance histogram, 6 = exposure state.
layout(std430, set = 1, binding = 5) buffer LuminanceHistogramBuffer
{
	uint Bins[HISTOGRAM_BINS];
} b_Histogram;

layout(push_constant) uniform Uniforms
{
	float MinLogLuminance;            // log2 luminance mapped to bin 1
	float InverseLogLuminanceRange;   // 1 / (maxLogLuminance - minLogLuminance)
	uint  InputWidth;
	uint  InputHeight;
} u_Uniforms;

shared uint s_Histogram[HISTOGRAM_BINS];

float Luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Map a luminance value to a histogram bin. Bin 0 collects near-black pixels so
// they can be excluded from the average.
uint LuminanceToBin(float luminance)
{
	if (luminance < LUM_EPSILON)
		return 0u;

	float logContribution = clamp((log2(luminance) - u_Uniforms.MinLogLuminance) * u_Uniforms.InverseLogLuminanceRange, 0.0, 1.0);
	return uint(logContribution * 254.0 + 1.0);
}

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
void main()
{
	s_Histogram[gl_LocalInvocationIndex] = 0u;
	barrier();

	uvec2 coord = gl_GlobalInvocationID.xy;
	if (coord.x < u_Uniforms.InputWidth && coord.y < u_Uniforms.InputHeight)
	{
		vec2 uv = (vec2(coord) + 0.5) / vec2(float(u_Uniforms.InputWidth), float(u_Uniforms.InputHeight));
		vec3 color = SampleLinear(u_SceneColor, uv).rgb;
		uint bin = LuminanceToBin(Luminance(color));
		atomicAdd(s_Histogram[bin], 1u);
	}

	barrier();
	atomicAdd(b_Histogram.Bins[gl_LocalInvocationIndex], s_Histogram[gl_LocalInvocationIndex]);
}
