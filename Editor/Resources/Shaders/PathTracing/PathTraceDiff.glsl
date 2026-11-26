// -----------------------------------------------------
// -- Lux path tracer diff: raster frame vs reference --
// -----------------------------------------------------
//
// Compares the raster frame (scene colour before the path tracer replaces it, linear HDR) with the
// converged path-traced reference, per pixel, in linear light clamped to [0, 1]:
//   * writes an error heat map (blue = equal, red = large) to o_DiffImage;
//   * writes each workgroup's sum of |error| and error^2 to a host-visible buffer, which the CPU adds up
//     into MAE / PSNR (no atomics, no precision limits).
// The raster frame is whatever indirect-diffuse source is active (environment, lightmaps, realtime GI),
// so "Diff Lightmap" and "Diff Realtime GI" are this pass with that source selected. See docs/GI_PLAN.md.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 67) uniform texture2D u_DiffRasterColor;
layout(set = 1, binding = 68) uniform texture2D u_DiffReferenceColor;
layout(rgba16f, set = 1, binding = 69) uniform writeonly image2D o_DiffImage;
layout(std430, set = 1, binding = 70) writeonly buffer DiffPartials { vec2 Partials[]; };

layout(push_constant) uniform DiffPush
{
	uvec4 SizeGroups; // width, height, groups per row, unused
	vec4 Params;      // x: error value mapped to full red, y: 1 / peak (values are divided by it before the [0, 1] clamp)
} u_Push;

shared float s_Abs[64];
shared float s_Sq[64];

vec3 Heat(float t)
{
	t = clamp(t, 0.0, 1.0);
	// blue -> cyan -> green -> yellow -> red
	return clamp(vec3(1.5 - abs(4.0 * t - 3.0), 1.5 - abs(4.0 * t - 2.0), 1.5 - abs(4.0 * t - 1.0)), 0.0, 1.0);
}

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	const uint local = gl_LocalInvocationIndex;
	float absError = 0.0;
	float sqError = 0.0;

	if (pixel.x < u_Push.SizeGroups.x && pixel.y < u_Push.SizeGroups.y)
	{
		// Linear HDR divided by the peak (GIL9): a peak above 1 keeps bright errors from clipping.
		vec3 raster = clamp(texelFetch(u_DiffRasterColor, ivec2(pixel), 0).rgb * u_Push.Params.y, 0.0, 1.0);
		vec3 reference = clamp(texelFetch(u_DiffReferenceColor, ivec2(pixel), 0).rgb * u_Push.Params.y, 0.0, 1.0);
		vec3 difference = abs(raster - reference);
		absError = (difference.r + difference.g + difference.b) * (1.0 / 3.0);
		sqError = dot(difference, difference) * (1.0 / 3.0);
		imageStore(o_DiffImage, ivec2(pixel), vec4(Heat(absError / max(u_Push.Params.x, 1e-4)), 1.0));
	}

	s_Abs[local] = absError;
	s_Sq[local] = sqError;
	barrier();
	for (uint stride = 32u; stride > 0u; stride >>= 1u)
	{
		if (local < stride)
		{
			s_Abs[local] += s_Abs[local + stride];
			s_Sq[local] += s_Sq[local + stride];
		}
		barrier();
	}
	if (local == 0u)
		Partials[gl_WorkGroupID.y * u_Push.SizeGroups.z + gl_WorkGroupID.x] = vec2(s_Abs[0], s_Sq[0]);
}
