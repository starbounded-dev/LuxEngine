#version 450 core
#pragma stage : comp

#include <Buffers.glslh>
#include <Samplers.glslh>

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 0) uniform utexture2D u_CurrentAO;
layout(set = 1, binding = 1) uniform utexture2D u_HistoryAO;
layout(set = 1, binding = 2) uniform texture2D u_Depth;
layout(set = 1, binding = 3, r32ui) uniform writeonly uimage2D o_HistoryAO;

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

uint BlendVisibility(uint currentValue, uint historyValue, float blendFactor, bool bentNormals)
{
	uint currentVisibility = bentNormals ? ((currentValue >> 24) & 0xffu) : (currentValue & 0xffu);
	uint historyVisibility = bentNormals ? ((historyValue >> 24) & 0xffu) : (historyValue & 0xffu);
	uint blendedVisibility = uint(round(mix(float(currentVisibility), float(historyVisibility), blendFactor)));
	blendedVisibility = clamp(blendedVisibility, 0u, 255u);

	if (bentNormals)
		return (currentValue & 0x00ffffffu) | (blendedVisibility << 24);

	return blendedVisibility;
}

void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 outputSize = imageSize(o_HistoryAO);
	if (pixel.x >= outputSize.x || pixel.y >= outputSize.y)
		return;

	vec2 uv = (vec2(pixel) + 0.5) / vec2(outputSize);
	uint currentValue = texture(usampler2D(u_CurrentAO, r_PointSampler), uv).x;
	uint outputValue = currentValue;

	vec2 historyUV = ReprojectHistoryUV(uv);
	bool validHistory = u_Settings.HasHistory != 0u
		&& all(greaterThanEqual(historyUV, vec2(0.0)))
		&& all(lessThanEqual(historyUV, vec2(1.0)));

	if (validHistory)
	{
		uint historyValue = texture(usampler2D(u_HistoryAO, r_PointSampler), historyUV).x;
		outputValue = BlendVisibility(currentValue, historyValue, clamp(u_Settings.Blend, 0.0, 0.98), u_Settings.BentNormals != 0u);
	}

	imageStore(o_HistoryAO, pixel, uvec4(outputValue));
}
