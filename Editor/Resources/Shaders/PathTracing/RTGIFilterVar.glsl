// ----------------------------------------------------------
// -- Lux RTGI spatiotemporal denoiser: variance-guided a-trous --
// ----------------------------------------------------------
//
// One a-trous iteration (5x5, taps `step` pixels apart) of the temporally accumulated estimate, as RTGIFilter.glsl but with the
// SVGF luminance edge-stopping weight, which relaxes where the variance is high and tightens where it is low, and the
// variance filtered along with the colour (its alpha channel is 1 + variance). Mode 1 additionally adds
// albedo * (1 - metalness) * filtered to scene colour and writes the result to o_RTGIVarFinal. See docs/GI_PLAN.md.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 93) uniform texture2D u_RTGIInput;      // rgb estimate, a = 1 + variance (0 = no surface)
layout(set = 1, binding = 94) uniform texture2D u_RTGIAlbedoTex;  // rgb albedo * (1 - metalness), a = hit distance
layout(set = 1, binding = 95) uniform texture2D u_RTGINormalTex;  // xyz world normal
layout(set = 1, binding = 96) uniform texture2D u_RTGISceneColor;
layout(rgba32f, set = 1, binding = 128) uniform writeonly image2D o_RTGIVarOut;
layout(rgba16f, set = 1, binding = 129) uniform writeonly image2D o_RTGIVarFinal;

layout(push_constant) uniform RTGIFilterPush
{
	uvec4 SizeModeStep; // width, height, mode (0 filter, 1 filter + add), step in pixels
	vec4 Params;        // x: normal sharpness, y: relative distance tolerance, z: luminance sharpness (sigma_l)
} u_Push;

float Lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Push.SizeModeStep.xy);
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	const vec4 centre = texelFetch(u_RTGIInput, pixel, 0);
	vec3 filtered = centre.rgb;
	float filteredVariance = max(centre.a - 1.0, 0.0);
	if (centre.a > 0.5)
	{
		const vec3 n0 = texelFetch(u_RTGINormalTex, pixel, 0).xyz;
		const float d0 = texelFetch(u_RTGIAlbedoTex, pixel, 0).a;
		const float lum0 = Lum(centre.rgb);
		const float sigma = u_Push.Params.z * sqrt(max(centre.a - 1.0, 0.0)) + 1e-4;
		const float kernel[5] = float[5](1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0);
		vec3 sum = vec3(0.0);
		float weightSum = 0.0, varianceSum = 0.0;
		for (int y = -2; y <= 2; y++)
		{
			for (int x = -2; x <= 2; x++)
			{
				const ivec2 p = pixel + ivec2(x, y) * int(u_Push.SizeModeStep.w);
				if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y)
					continue;
				const vec4 sampleValue = texelFetch(u_RTGIInput, p, 0);
				if (sampleValue.a < 0.5)
					continue;
				const vec3 n1 = texelFetch(u_RTGINormalTex, p, 0).xyz;
				const float d1 = texelFetch(u_RTGIAlbedoTex, p, 0).a;
				const float wN = pow(max(dot(n0, n1), 0.0), u_Push.Params.x);
				const float wZ = exp(-abs(d0 - d1) / (u_Push.Params.y * max(d0, 0.1) + 1e-3));
				const float wL = exp(-abs(lum0 - Lum(sampleValue.rgb)) / sigma);
				const float w = kernel[x + 2] * kernel[y + 2] * wN * wZ * wL;
				sum += sampleValue.rgb * w;
				varianceSum += max(sampleValue.a - 1.0, 0.0) * w * w;
				weightSum += w;
			}
		}
		if (weightSum > 1e-6)
		{
			filtered = sum / weightSum;
			filteredVariance = varianceSum / (weightSum * weightSum);
		}
	}

	if (u_Push.SizeModeStep.z == 0u)
	{
		imageStore(o_RTGIVarOut, pixel, vec4(filtered, centre.a > 0.5 ? 1.0 + filteredVariance : 0.0));
		return;
	}

	vec4 scene = texelFetch(u_RTGISceneColor, pixel, 0);
	if (centre.a > 0.5)
		scene.rgb += filtered * texelFetch(u_RTGIAlbedoTex, pixel, 0).rgb;
	imageStore(o_RTGIVarFinal, pixel, scene);
}
