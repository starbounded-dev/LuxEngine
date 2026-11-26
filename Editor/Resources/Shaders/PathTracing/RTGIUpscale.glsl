#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <Buffers.glslh>

layout(set = 1, binding = 94) uniform texture2D u_RTGIAlbedoTex;
layout(set = 1, binding = 95) uniform texture2D u_RTGINormalTex;
layout(set = 1, binding = 96) uniform texture2D u_RTGISceneColor;
layout(set = 1, binding = 172) uniform texture2D u_RTGIUpscaleInput;
layout(set = 1, binding = 173) uniform texture2D u_RTGIUpscaleDepth;
layout(set = 1, binding = 174) uniform texture2D u_RTGIUpscaleNormal;
layout(set = 1, binding = 175) uniform texture2D u_RTGIUpscaleBaseColor;
layout(set = 1, binding = 176) uniform texture2D u_RTGIUpscaleMetalRough;
layout(rgba16f, set = 1, binding = 129) uniform writeonly image2D o_RTGIVarFinal;

vec3 ViewPosition(vec2 uv, float deviceDepth)
{
	const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	const vec4 position = u_Camera.InverseProjectionMatrix * vec4(ndc, deviceDepth, 1.0);
	return position.xyz / position.w;
}

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = imageSize(o_RTGIVarFinal);
	if (any(greaterThanEqual(pixel, size)))
		return;
	vec4 scene = texelFetch(u_RTGISceneColor, pixel, 0);
	const float deviceDepth = texelFetch(u_RTGIUpscaleDepth, pixel, 0).r;
	if (deviceDepth > 1e-6)
	{
		const vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
		const vec3 position = ViewPosition(uv, deviceDepth);
		const vec3 normal = texelFetch(u_RTGIUpscaleNormal, pixel, 0).xyz;
		const ivec2 traceSize = textureSize(u_RTGIUpscaleInput, 0);
		const vec2 sampleCentre = uv * vec2(traceSize) - 0.5;
		const ivec2 centre = ivec2(floor(sampleCentre + 0.5));
		const float planeTolerance = 0.01 + 0.015 * abs(position.z);
		vec3 irradiance = vec3(0.0);
		float weightSum = 0.0;
		for (int y = -1; y <= 1; y++)
		{
			for (int x = -1; x <= 1; x++)
			{
				const ivec2 q = centre + ivec2(x, y);
				if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, traceSize)))
					continue;
				const vec4 estimate = texelFetch(u_RTGIUpscaleInput, q, 0);
				if (estimate.a < 0.5)
					continue;
				const vec3 qNormal = mat3(u_Camera.ViewMatrix) * texelFetch(u_RTGINormalTex, q, 0).xyz;
				const float similarity = dot(normal, qNormal);
				if (similarity < 0.5)
					continue;
				const vec2 qUV = (vec2(q) + 0.5) / vec2(traceSize);
				const float distance = texelFetch(u_RTGIAlbedoTex, q, 0).a;
				const vec3 qPosition = normalize(ViewPosition(qUV, 1.0)) * distance;
				const float planeDistance = abs(dot(position - qPosition, qNormal));
				if (planeDistance > planeTolerance)
					continue;
				const vec2 offset = vec2(q) - sampleCentre;
				const float weight = exp(-1.5 * dot(offset, offset) - 4.0 * planeDistance / planeTolerance) * pow(similarity, 8.0);
				irradiance += estimate.rgb * weight;
				weightSum += weight;
			}
		}
		const vec3 diffuse = texelFetch(u_RTGIUpscaleBaseColor, pixel, 0).rgb *
			(1.0 - texelFetch(u_RTGIUpscaleMetalRough, pixel, 0).r);
		scene.rgb += diffuse * irradiance / max(weightSum, 1e-6);
	}
	imageStore(o_RTGIVarFinal, pixel, scene);
}
