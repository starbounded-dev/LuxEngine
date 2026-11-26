#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <Buffers.glslh>

layout(set = 1, binding = 177) uniform texture2D u_HeightFogScene;
layout(set = 1, binding = 178) uniform texture2D u_HeightFogDepth;
layout(rgba16f, set = 1, binding = 179) uniform writeonly image2D o_HeightFogScene;

layout(push_constant) uniform HeightFogSettings
{
	vec4 Medium; // density at reference height, reference height, falloff, maximum distance
	vec4 ColourStart; // linear fog radiance, start distance
} u_HeightFog;

float FogTransmittance(float entryHeight, float exitHeight, float distance)
{
	const vec2 densityExponents = -u_HeightFog.Medium.z * (vec2(entryHeight, exitHeight) - u_HeightFog.Medium.y);
	const float difference = abs(densityExponents.y - densityExponents.x);
	const float span = difference > 1e-3 ? (1.0 - exp(-difference)) / difference : exp(-0.5 * difference);
	const float peakDensity = u_HeightFog.Medium.x * exp(clamp(max(densityExponents.x, densityExponents.y), -80.0, 40.0));
	return exp(-min(distance * peakDensity * span, 80.0));
}

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = imageSize(o_HeightFogScene);
	if (any(greaterThanEqual(pixel, size)))
		return;

	const vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
	const vec2 ndc = vec2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
	const vec4 nearPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	const bool orthographic = u_Camera.IsOrthographic != 0;
	const vec3 origin = orthographic ? nearPoint.xyz / nearPoint.w : vec3(0.0);
	const vec3 direction = orthographic ? vec3(0.0, 0.0, -1.0) : normalize(nearPoint.xyz / nearPoint.w);
	const float deviceDepth = texelFetch(u_HeightFogDepth, pixel, 0).r;
	float travel = u_HeightFog.Medium.w;
	if (deviceDepth > 1e-6)
	{
		const vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, deviceDepth, 1.0);
		travel = min(travel, dot(viewPoint.xyz / viewPoint.w - origin, direction));
	}

	vec4 scene = texelFetch(u_HeightFogScene, pixel, 0);
	const float entry = u_HeightFog.ColourStart.w;
	if (travel > entry)
	{
		const float originHeight = (u_Camera.InverseViewMatrix * vec4(origin, 1.0)).y;
		const float raySlope = (u_Camera.InverseViewMatrix * vec4(direction, 0.0)).y;
		const float transmittance = FogTransmittance(originHeight + entry * raySlope, originHeight + travel * raySlope, travel - entry);
		scene.rgb = mix(u_HeightFog.ColourStart.rgb, scene.rgb, transmittance);
	}
	imageStore(o_HeightFogScene, pixel, scene);
}
