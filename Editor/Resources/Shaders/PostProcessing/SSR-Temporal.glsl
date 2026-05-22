#version 450 core
#pragma stage : comp

#include <Buffers.glslh>
#include <Samplers.glslh>

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 0) uniform texture2D u_CurrentSSR;
layout(set = 1, binding = 1) uniform texture2D u_HistorySSR;
layout(set = 1, binding = 2) uniform texture2D u_Depth;
layout(set = 1, binding = 3, rgba16f) uniform writeonly image2D o_HistorySSR;

layout(push_constant) uniform TemporalAccumulationSettings
{
	mat4 PreviousViewProjection;
	float Blend;
	uint HasHistory;
	uint BentNormals;
	uint ResolutionScale;
} u_Settings;

vec2 ReprojectHistoryUV(vec2 uv)
{
	float depth = texture(sampler2D(u_Depth, r_PointSampler), uv).r;
	vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec4 world = u_Camera.InverseViewProjectionMatrix * clip;
	world /= max(world.w, 0.00001);

	vec4 previousClip = u_Settings.PreviousViewProjection * world;
	if (previousClip.w <= 0.00001)
		return vec2(-1.0);

	return previousClip.xy / previousClip.w * 0.5 + 0.5;
}

void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 outputSize = imageSize(o_HistorySSR);
	if (pixel.x >= outputSize.x || pixel.y >= outputSize.y)
		return;

	vec2 uv = (vec2(pixel) + 0.5) / vec2(outputSize);
	vec4 currentValue = texture(sampler2D(u_CurrentSSR, r_LinearSampler), uv);
	vec4 outputValue = currentValue;

	vec2 historyUV = ReprojectHistoryUV(uv);
	bool validHistory = u_Settings.HasHistory != 0u
		&& all(greaterThanEqual(historyUV, vec2(0.0)))
		&& all(lessThanEqual(historyUV, vec2(1.0)));

	if (validHistory)
	{
		vec4 historyValue = texture(sampler2D(u_HistorySSR, r_LinearSampler), historyUV);
		float confidence = clamp(currentValue.a, 0.0, 1.0);
		float historyWeight = clamp(u_Settings.Blend, 0.0, 0.98) * confidence;
		outputValue = mix(currentValue, historyValue, historyWeight);
		outputValue.a = max(currentValue.a, historyValue.a * historyWeight);
	}

	imageStore(o_HistorySSR, pixel, outputValue);
}
