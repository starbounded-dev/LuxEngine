#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <Buffers.glslh>

layout(set = 1, binding = 150) uniform texture2D u_RRDepth;
layout(set = 1, binding = 151) uniform texture2D u_RRNormal;
layout(set = 1, binding = 152) uniform texture2D u_RRMetalRough;
layout(set = 1, binding = 153) uniform texture2D u_RRBaseColor;
layout(set = 1, binding = 154) uniform texture2D u_RRSceneColor;
layout(rgba16f, set = 1, binding = 155) uniform writeonly image2D o_RRResult;
layout(set = 1, binding = 156) uniform texture2D u_RRCorrection;

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = imageSize(o_RRResult);
	if (any(greaterThanEqual(pixel, size)))
		return;

	vec4 scene = texelFetch(u_RRSceneColor, pixel, 0);
	const vec4 centre = texelFetch(u_RRCorrection, pixel, 0);
	if (centre.a < 0.5)
	{
		imageStore(o_RRResult, pixel, vec4(centre.rgb, scene.a));
		return;
	}

	const vec3 normal = texelFetch(u_RRNormal, pixel, 0).xyz;
	const float depth = LinearizeDeviceDepth(texelFetch(u_RRDepth, pixel, 0).r);
	const vec2 metalRough = texelFetch(u_RRMetalRough, pixel, 0).xy;
	const vec3 albedo = texelFetch(u_RRBaseColor, pixel, 0).rgb;
	const int stepSize = metalRough.y >= 0.25 ? 2 : 1;
	const float kernel[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);

	// Filter the signed reflection correction. The guides reject neighbouring objects, material boundaries,
	// and silhouettes before the correction is added to the unchanged raster lighting.
	vec3 sum = centre.rgb * 36.0;
	float totalWeight = 36.0;
	for (int y = -2; y <= 2; y++)
	{
		for (int x = -2; x <= 2; x++)
		{
			if (x == 0 && y == 0)
				continue;
			const ivec2 q = pixel + ivec2(x, y) * stepSize;
			if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size)))
				continue;
			const vec4 correction = texelFetch(u_RRCorrection, q, 0);
			if (correction.a < 0.5)
				continue;

			const vec3 n = texelFetch(u_RRNormal, q, 0).xyz;
			const float z = LinearizeDeviceDepth(texelFetch(u_RRDepth, q, 0).r);
			const vec2 materialDelta = abs(texelFetch(u_RRMetalRough, q, 0).xy - metalRough);
			const float albedoDelta = length(texelFetch(u_RRBaseColor, q, 0).rgb - albedo);
			const float relativeDepth = abs(z - depth) / max(depth, 0.1);
			const float weight = kernel[x + 2] * kernel[y + 2] * pow(max(dot(normal, n), 0.0), 32.0)
				* exp(-60.0 * relativeDepth - 20.0 * (materialDelta.x + materialDelta.y) - 15.0 * albedoDelta);
			sum += correction.rgb * weight;
			totalWeight += weight;
		}
	}
	scene.rgb = max(scene.rgb + sum / totalWeight, vec3(0.0));
	imageStore(o_RRResult, pixel, scene);
}
