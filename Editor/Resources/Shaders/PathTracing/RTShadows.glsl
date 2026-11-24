// ----------------------------------------------------
// -- Lux ray-traced directional shadows             --
// ----------------------------------------------------
//
// Writes a visibility mask (r: 1 lit, 0 shadowed) for the directional light, from the raster depth and G-buffer normal, so it lines up
// with raster pixels. Each pixel traces a few shadow rays toward directions spread uniformly over the light's angular size, which
// gives physically sized penumbrae (and no cascade range, resolution, peter-panning or seams). Deferred lighting reads the mask in
// place of the shadow-map cascades when the option is on. See docs/GI_PLAN.md.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <PathTraceCommon.glslh>

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

layout(set = 1, binding = 160) uniform texture2D u_RSDepth;   // device depth (reverse-Z: 0 = sky)
layout(set = 1, binding = 161) uniform texture2D u_RSNormal;  // view-space normal
layout(rgba16f, set = 1, binding = 162) uniform writeonly image2D o_RSMask;

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Push.SizeFrameBounces.xy);
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	const float depth = texelFetch(u_RSDepth, pixel, 0).r;
	if (depth <= 1e-6)
	{
		imageStore(o_RSMask, pixel, vec4(1.0));
		return;
	}
	const vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);
	const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	const vec4 world = u_Camera.InverseViewProjectionMatrix * vec4(ndc, depth, 1.0);
	const vec3 P = world.xyz / world.w;
	const vec3 N = normalize(mat3(u_Camera.InverseViewMatrix) * texelFetch(u_RSNormal, pixel, 0).xyz);
	const vec3 toLight = normalize(u_Push.SkyRadiance.xyz);
	const float angularRadius = u_Push.SkyRadiance.w;
	if (dot(N, toLight) <= 0.0)
	{
		imageStore(o_RSMask, pixel, vec4(1.0)); // facing away: the lighting zeroes it
		return;
	}

	g_Rng = PCG(uint(pixel.x) + PCG(uint(pixel.y) + PCG(u_Push.SizeFrameBounces.z * 9157u + 7u)));
	vec3 t, b;
	Basis(toLight, t, b);
	const uint rays = max(u_Push.Counts.z, 1u);
	float visible = 0.0;
	for (uint k = 0u; k < rays; k++)
	{
		// uniform over the cone of half angle angularRadius around the light direction
		const float cosMax = cos(angularRadius);
		const float cosT = 1.0 - Rand() * (1.0 - cosMax);
		const float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
		const float phi = 2.0 * PI * Rand();
		const vec3 L = normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + toLight * cosT);
		if (!Occluded(P + N * 2e-3 + L * 1e-3, L, 1.0e30))
			visible += 1.0;
	}
	imageStore(o_RSMask, pixel, vec4(vec3(visible / float(rays)), 1.0));
}
