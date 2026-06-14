// Reduces the luminance histogram to an average luminance, temporally adapts it,
// and writes the resulting exposure multiplier into a persistent state buffer
// consumed by SceneComposite.glsl. Also clears the histogram for the next frame.
// Single workgroup of 256 threads (one per histogram bin).
#version 450 core
#pragma stage : comp

#define HISTOGRAM_BINS 256

// Storage-buffer (set,binding) slots are global in this engine (see LuminanceHistogram.glsl).
// 5 = luminance histogram, 6 = exposure state — must match every other shader that uses them.
layout(std430, set = 1, binding = 5) buffer LuminanceHistogramBuffer
{
	uint Bins[HISTOGRAM_BINS];
} b_Histogram;

layout(std430, set = 1, binding = 6) buffer ExposureStateBuffer
{
	float AdaptedLuminance;
	float Exposure;
} b_Exposure;

layout(push_constant) uniform Uniforms
{
	float MinLogLuminance;
	float LogLuminanceRange;
	float TimeDelta;
	float SpeedUp;
	float SpeedDown;
	float MinEV100;
	float MaxEV100;
	uint  PixelCount;
} u_Uniforms;

shared uint s_Histogram[HISTOGRAM_BINS];

layout(local_size_x = HISTOGRAM_BINS, local_size_y = 1, local_size_z = 1) in;
void main()
{
	uint localIndex = gl_LocalInvocationIndex;

	// Weight each bin by its index so the reduction yields a sum of (bin * count).
	uint countForThisBin = b_Histogram.Bins[localIndex];
	s_Histogram[localIndex] = countForThisBin * localIndex;
	barrier();

	for (uint cutoff = (HISTOGRAM_BINS >> 1); cutoff > 0u; cutoff >>= 1u)
	{
		if (localIndex < cutoff)
			s_Histogram[localIndex] += s_Histogram[localIndex + cutoff];
		barrier();
	}

	// Clear the global histogram for the next frame (each thread clears its bin).
	b_Histogram.Bins[localIndex] = 0u;

	if (localIndex == 0u)
	{
		// Bin 0 holds near-black pixels; exclude them from the average.
		float blackCount = float(countForThisBin);
		float denom = max(float(u_Uniforms.PixelCount) - blackCount, 1.0);
		float weightedLogAverage = (float(s_Histogram[0]) / denom) - 1.0;

		float avgLuminance = exp2(((weightedLogAverage / 254.0) * u_Uniforms.LogLuminanceRange) + u_Uniforms.MinLogLuminance);
		avgLuminance = max(avgLuminance, 1e-4);

		// Temporal adaptation in luminance space.
		float prevLuminance = b_Exposure.AdaptedLuminance;
		if (!(prevLuminance > 0.0) || isinf(prevLuminance) || isnan(prevLuminance))
			prevLuminance = avgLuminance;

		float speed = (avgLuminance > prevLuminance) ? u_Uniforms.SpeedUp : u_Uniforms.SpeedDown;
		float adaptation = clamp(1.0 - exp(-max(u_Uniforms.TimeDelta, 0.0) * max(speed, 0.0)), 0.0, 1.0);
		float adaptedLuminance = mix(prevLuminance, avgLuminance, adaptation);

		// Luminance -> EV100 (reflected-light meter, K = 12.5), clamp, then -> exposure.
		float ev100 = log2(adaptedLuminance * 100.0 / 12.5);
		ev100 = clamp(ev100, u_Uniforms.MinEV100, u_Uniforms.MaxEV100);
		float exposure = 1.0 / (1.2 * exp2(ev100));

		b_Exposure.AdaptedLuminance = adaptedLuminance;
		b_Exposure.Exposure = exposure;
	}
}
