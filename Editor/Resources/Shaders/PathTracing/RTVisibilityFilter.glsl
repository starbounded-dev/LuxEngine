#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <Buffers.glslh>

layout(set = 1, binding = 160) uniform texture2D u_RSDepth;
layout(set = 1, binding = 161) uniform texture2D u_RSNormal;
layout(rgba16f, set = 1, binding = 162) uniform writeonly image2D o_RSMask;
layout(set = 1, binding = 163) uniform texture2D u_RSRawMask;

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = imageSize(o_RSMask);
	if (any(greaterThanEqual(pixel, size)))
		return;

	const float rawDepth = texelFetch(u_RSDepth, pixel, 0).r;
	if (rawDepth <= 1e-6)
	{
		imageStore(o_RSMask, pixel, vec4(1.0));
		return;
	}
	const float depth = LinearizeDeviceDepth(rawDepth);
	const vec3 normal = texelFetch(u_RSNormal, pixel, 0).xyz;
	vec4 sum = texelFetch(u_RSRawMask, pixel, 0) * 4.0;
	float totalWeight = 4.0;
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			if (x == 0 && y == 0)
				continue;
			const ivec2 q = pixel + ivec2(x, y);
			if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size)))
				continue;
			const float z = texelFetch(u_RSDepth, q, 0).r;
			if (z <= 1e-6)
				continue;
			const vec3 n = texelFetch(u_RSNormal, q, 0).xyz;
			const float relativeDepth = abs(LinearizeDeviceDepth(z) - depth) / max(depth, 0.1);
			const float weight = float(3 - abs(x) - abs(y)) * pow(max(dot(normal, n), 0.0), 32.0) * exp(-100.0 * relativeDepth);
			sum += texelFetch(u_RSRawMask, q, 0) * weight;
			totalWeight += weight;
		}
	}
	imageStore(o_RSMask, pixel, sum / totalWeight);
}
