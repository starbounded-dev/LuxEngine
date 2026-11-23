// ------------------------------------------------------
// -- Lux RTGI spatiotemporal denoiser: temporal pass  --
// ------------------------------------------------------
//
// First half of an SVGF-style denoiser (Schied et al. 2017) for the ray-traced GI estimate. Instead of restarting the running
// mean whenever the camera moves, the previous frame's accumulated estimate is reprojected (previous view-projection matrix
// and the visible surface position) and blended with the new noisy estimate, after normal and depth tests reject
// disocclusions. Luminance moments ride along, so the variance of the result is known: RTGIFilterVar.glsl then runs a
// variance-guided a-trous filter over it. See docs/GI_PLAN.md.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Identical to the Camera block in Buffers.glslh (same (set,binding), same name).
layout(std140, set = 2, binding = 0) uniform Camera
{
	mat4 ViewProjectionMatrix;
	mat4 InverseViewProjectionMatrix;
	mat4 ProjectionMatrix;
	mat4 InverseProjectionMatrix;
	mat4 ViewMatrix;
	mat4 InverseViewMatrix;
	vec2 NDCToViewMul;
	vec2 NDCToViewAdd;
	vec2 DepthUnpackConsts;
	vec2 CameraTanHalfFOV;
	mat4 UnjitteredViewProjectionMatrix;
	mat4 PreviousViewProjectionMatrix;
	vec2 Jitter;
	vec2 PreviousJitter;
	int IsOrthographic;
} u_Camera;

layout(set = 1, binding = 94) uniform texture2D u_RTGIAlbedoTex;  // rgb albedo * (1 - metalness), a = hit distance
layout(set = 1, binding = 95) uniform texture2D u_RTGINormalTex;  // xyz world normal
layout(set = 1, binding = 120) uniform texture2D u_TDNoisy;       // this frame's estimate: rgb, a = valid
layout(set = 1, binding = 121) uniform texture2D u_TDPrev0;       // previous history: rgb colour, a = age
layout(set = 1, binding = 122) uniform texture2D u_TDPrev1;       // previous: x luminance mean, y luminance mean of squares, z view depth
layout(set = 1, binding = 123) uniform texture2D u_TDPrevNormal;
layout(rgba16f, set = 1, binding = 124) uniform writeonly image2D o_TDHist0;
layout(rgba32f, set = 1, binding = 125) uniform writeonly image2D o_TDHist1;
layout(rgba16f, set = 1, binding = 126) uniform writeonly image2D o_TDNormal;
layout(rgba32f, set = 1, binding = 127) uniform writeonly image2D o_TDDenoised; // rgb colour, a = 1 + variance (0 = no surface)

layout(push_constant) uniform RTGITemporalPush
{
	uvec4 SizeValidAge; // width, height, history valid (0/1), maximum age (frames averaged)
} u_Push;

float Lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Push.SizeValidAge.xy);
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	const vec4 noisy = texelFetch(u_TDNoisy, pixel, 0);
	if (noisy.a < 0.5)
	{
		imageStore(o_TDHist0, pixel, vec4(0.0));
		imageStore(o_TDHist1, pixel, vec4(0.0));
		imageStore(o_TDNormal, pixel, vec4(0.0));
		imageStore(o_TDDenoised, pixel, vec4(0.0));
		return;
	}

	// World position of the visible surface from the camera ray and the hit distance the trace stored.
	const vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);
	const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	const vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	const vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
	const vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	const vec3 dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);
	const float dist = texelFetch(u_RTGIAlbedoTex, pixel, 0).a;
	const vec3 position = origin + dir * dist;
	const vec3 normal = texelFetch(u_RTGINormalTex, pixel, 0).xyz;
	const float depth = (u_Camera.ViewProjectionMatrix * vec4(position, 1.0)).w;
	const float lum = Lum(noisy.rgb);

	vec3 history = vec3(0.0);
	vec2 moments = vec2(0.0);
	float age = 0.0, weightSum = 0.0;
	if (u_Push.SizeValidAge.z != 0u)
	{
		const vec4 prevClip = u_Camera.PreviousViewProjectionMatrix * vec4(position, 1.0);
		if (prevClip.w > 1e-4)
		{
			const vec2 prevNdc = prevClip.xy / prevClip.w;
			const vec2 prevPos = vec2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5) * vec2(size) - 0.5;
			const ivec2 base = ivec2(floor(prevPos));
			const vec2 f = prevPos - vec2(base);
			for (int i = 0; i < 4; i++)
			{
				const ivec2 q = base + ivec2(i & 1, i >> 1);
				if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y)
					continue;
				const vec4 prev1 = texelFetch(u_TDPrev1, q, 0);
				if (prev1.z <= 0.0 || dot(texelFetch(u_TDPrevNormal, q, 0).xyz, normal) < 0.9 || abs(prev1.z - prevClip.w) > 0.05 * prevClip.w + 0.02)
					continue;
				const float w = ((i & 1) != 0 ? f.x : 1.0 - f.x) * ((i >> 1) != 0 ? f.y : 1.0 - f.y);
				const vec4 prev0 = texelFetch(u_TDPrev0, q, 0);
				history += prev0.rgb * w;
				age += prev0.a * w;
				moments += prev1.xy * w;
				weightSum += w;
			}
		}
	}

	vec3 color = noisy.rgb;
	vec2 newMoments = vec2(lum, lum * lum);
	float newAge = 1.0;
	if (weightSum > 1e-3)
	{
		history /= weightSum;
		moments /= weightSum;
		age /= weightSum;
		newAge = min(age + 1.0, float(max(u_Push.SizeValidAge.w, 1u)));
		const float alpha = 1.0 / newAge;
		color = mix(history, noisy.rgb, alpha);
		newMoments = mix(moments, newMoments, alpha);
	}

	float variance = max(newMoments.y - newMoments.x * newMoments.x, 0.0);
	if (newAge < 4.0)
	{
		// Too little history for a temporal variance: use the spatial variance of the noisy 3x3 neighbourhood.
		float sum = 0.0, sumSq = 0.0, count = 0.0;
		for (int y = -1; y <= 1; y++)
		{
			for (int x = -1; x <= 1; x++)
			{
				const ivec2 p = pixel + ivec2(x, y);
				if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y)
					continue;
				const vec4 n = texelFetch(u_TDNoisy, p, 0);
				if (n.a < 0.5)
					continue;
				const float l = Lum(n.rgb);
				sum += l;
				sumSq += l * l;
				count += 1.0;
			}
		}
		const float spatial = count > 0.0 ? max(sumSq / count - (sum / count) * (sum / count), 0.0) : 0.0;
		variance = mix(spatial, variance, clamp((newAge - 1.0) / 3.0, 0.0, 1.0));
	}

	imageStore(o_TDHist0, pixel, vec4(color, newAge));
	imageStore(o_TDHist1, pixel, vec4(newMoments, depth, 0.0));
	imageStore(o_TDNormal, pixel, vec4(normal, 0.0));
	imageStore(o_TDDenoised, pixel, vec4(color, 1.0 + variance));
}
