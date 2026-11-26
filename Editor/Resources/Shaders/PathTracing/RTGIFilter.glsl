// ------------------------------------------------
// -- Lux ray-traced GI: edge-aware denoise + add --
// ------------------------------------------------
//
// Mode 0: one a-trous iteration (5x5, taps `step` pixels apart) of the per-pixel indirect estimate, weighted
// by normal and distance so it never blurs across geometry edges. Mode 1: the same iteration, then
// scene colour += albedo * (1 - metalness) * filtered, written to o_RTGIOutput (the renderer copies it back
// into scene colour). Pixels the primary ray missed (the sky) are left alone. See docs/GI_PLAN.md, GI5.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 93) uniform texture2D u_RTGIInput;      // rgb estimate, a = valid
layout(set = 1, binding = 94) uniform texture2D u_RTGIAlbedoTex;  // rgb albedo * (1 - metalness), a = hit distance
layout(set = 1, binding = 95) uniform texture2D u_RTGINormalTex;  // xyz world normal
layout(set = 1, binding = 96) uniform texture2D u_RTGISceneColor;
layout(rgba16f, set = 1, binding = 97) uniform writeonly image2D o_RTGIOutput;

layout(push_constant) uniform RTGIFilterPush
{
	uvec4 SizeModeStep; // width, height, mode (0 filter, 1 filter + add), step in pixels
	vec4 Params;        // x: normal sharpness, y: relative distance tolerance
} u_Push;

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Push.SizeModeStep.xy);
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	vec4 centre = texelFetch(u_RTGIInput, pixel, 0);
	vec3 filtered = centre.rgb;
	if (centre.a > 0.5)
	{
		vec3 n0 = texelFetch(u_RTGINormalTex, pixel, 0).xyz;
		float d0 = texelFetch(u_RTGIAlbedoTex, pixel, 0).a;
		const float kernel[5] = float[5](1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0);
		vec3 sum = vec3(0.0);
		float weightSum = 0.0;
		for (int y = -2; y <= 2; y++)
		{
			for (int x = -2; x <= 2; x++)
			{
				ivec2 p = pixel + ivec2(x, y) * int(u_Push.SizeModeStep.w);
				if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y)
					continue;
				vec4 sampleValue = texelFetch(u_RTGIInput, p, 0);
				if (sampleValue.a < 0.5)
					continue;
				vec3 n1 = texelFetch(u_RTGINormalTex, p, 0).xyz;
				float d1 = texelFetch(u_RTGIAlbedoTex, p, 0).a;
				float wN = pow(max(dot(n0, n1), 0.0), u_Push.Params.x);
				float wZ = exp(-abs(d0 - d1) / (u_Push.Params.y * max(d0, 0.1) + 1e-3));
				float w = kernel[x + 2] * kernel[y + 2] * wN * wZ;
				sum += sampleValue.rgb * w;
				weightSum += w;
			}
		}
		if (weightSum > 1e-6)
			filtered = sum / weightSum;
	}

	if (u_Push.SizeModeStep.z == 0u)
	{
		imageStore(o_RTGIOutput, pixel, vec4(filtered, centre.a));
		return;
	}

	vec4 scene = texelFetch(u_RTGISceneColor, pixel, 0);
	if (centre.a > 0.5)
		scene.rgb += filtered * texelFetch(u_RTGIAlbedoTex, pixel, 0).rgb;
	imageStore(o_RTGIOutput, pixel, scene);
}
