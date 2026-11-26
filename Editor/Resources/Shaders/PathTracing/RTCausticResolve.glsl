#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <Buffers.glslh>

layout(set = 1, binding = 165) uniform utexture2D u_CausticFlux;
layout(set = 1, binding = 166) uniform texture2D u_CausticDepth;
layout(set = 1, binding = 167) uniform texture2D u_CausticNormal;
layout(set = 1, binding = 168) uniform texture2D u_CausticBaseColor;
layout(set = 1, binding = 169) uniform texture2D u_CausticMetalRough;
layout(set = 1, binding = 170) uniform texture2D u_CausticSceneColor;
layout(rgba16f, set = 1, binding = 171) uniform writeonly image2D o_CausticResult;

layout(push_constant) uniform CausticResolveSettings
{
	uvec4 PhotonCount;
} u_CausticResolve;

vec3 ReadFlux(ivec2 pixel, int width)
{
	return vec3(texelFetch(u_CausticFlux, pixel, 0).r,
		texelFetch(u_CausticFlux, pixel + ivec2(width, 0), 0).r,
		texelFetch(u_CausticFlux, pixel + ivec2(2 * width, 0), 0).r);
}

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = imageSize(o_CausticResult);
	if (any(greaterThanEqual(pixel, size)))
		return;
	vec4 scene = texelFetch(u_CausticSceneColor, pixel, 0);
	const float deviceDepth = texelFetch(u_CausticDepth, pixel, 0).r;
	if (deviceDepth <= 1e-6)
	{
		imageStore(o_CausticResult, pixel, scene);
		return;
	}

	const vec3 viewNormal = texelFetch(u_CausticNormal, pixel, 0).xyz;
	const float depth = LinearizeDeviceDepth(deviceDepth);
	const ivec2 storedSize = textureSize(u_CausticFlux, 0);
	const ivec2 fluxSize = ivec2(storedSize.x / 3, storedSize.y);
	const vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
	const vec2 centre = uv * vec2(fluxSize) - 0.5;
	vec3 flux = vec3(0.0);
	float weightSum = 0.0;
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			const ivec2 q = ivec2(floor(centre + 0.5)) + ivec2(x, y);
			if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, fluxSize)))
				continue;
			const ivec2 guidePixel = min(ivec2((vec2(q) + 0.5) / vec2(fluxSize) * vec2(size)), size - 1);
			const float z = texelFetch(u_CausticDepth, guidePixel, 0).r;
			if (z <= 1e-6)
				continue;
			const vec3 normal = texelFetch(u_CausticNormal, guidePixel, 0).xyz;
			const float dz = abs(LinearizeDeviceDepth(z) - depth) / max(depth, 0.1);
			const vec2 offset = vec2(q) - centre;
			const float weight = exp(-dot(offset, offset) - 60.0 * dz) * pow(max(dot(normal, viewNormal), 0.0), 32.0);
			flux += ReadFlux(q, fluxSize.x) * weight;
			weightSum += weight;
		}
	}

	const vec2 ndc = vec2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
	const vec4 viewPosition = u_Camera.InverseProjectionMatrix * vec4(ndc, deviceDepth, 1.0);
	const bool perspective = u_Camera.IsOrthographic == 0;
	const vec3 viewDirection = perspective ? normalize(-viewPosition.xyz / viewPosition.w) : vec3(0.0, 0.0, 1.0);
	const float scale = perspective ? depth : 1.0;
	const float projectedArea = 4.0 * scale * scale /
		(abs(u_Camera.ProjectionMatrix[0][0] * u_Camera.ProjectionMatrix[1][1]) * float(fluxSize.x) * float(fluxSize.y));
	const float receiverCosine = max(dot(viewNormal, viewDirection), 0.05);
	const float normalization = receiverCosine / max(projectedArea * 64.0 * float(u_CausticResolve.PhotonCount.x), 1e-6);
	const vec3 diffuse = texelFetch(u_CausticBaseColor, pixel, 0).rgb * (1.0 - texelFetch(u_CausticMetalRough, pixel, 0).r) / 3.14159265359;
	scene.rgb += diffuse * flux * (normalization / max(weightSum, 1e-6));
	imageStore(o_CausticResult, pixel, scene);
}
