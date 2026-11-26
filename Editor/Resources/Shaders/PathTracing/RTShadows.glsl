// ----------------------------------------------------
// -- Lux ray-traced raster visibility                --
// ----------------------------------------------------
//
// Writes a visibility mask (r: 1 lit, 0 shadowed) for the directional light, from the raster depth and G-buffer normal, so it lines up
// with raster pixels. Each pixel traces a few shadow rays toward directions spread uniformly over the light's angular size, which
// gives physically sized penumbrae (and no cascade range, resolution, peter-panning or seams). Deferred lighting reads the mask in
// place of the shadow-map cascades when the option is on. A finite-length directional ray augments the cascades with contact
// shadows. Independent cosine-weighted rays store local ambient visibility in alpha.
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

float LocalAmbientVisibility(vec3 origin, vec3 direction, float radius)
{
	rayQueryEXT query;
	rayQueryInitializeEXT(query, u_RTScene, gl_RayFlagsOpaqueEXT, 0xff, origin, 1e-4, direction, radius);
	while (rayQueryProceedEXT(query)) {}
	if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT)
		return 1.0;
	return smoothstep(0.0, radius, rayQueryGetIntersectionTEXT(query, true));
}

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
	g_Rng = PCG(uint(pixel.x) + PCG(uint(pixel.y) + PCG(u_Push.SizeFrameBounces.z * 9157u + 7u)));
	const vec3 origin = P + N * 2e-3;
	const vec3 toLight = u_Push.SkyRadiance.xyz;
	const float distanceLimit = u_Push.GroundRadiance.x;
	float sunVisibility = 1.0;
	if (distanceLimit > 0.0 && dot(N, toLight) > 0.0)
	{
		vec3 t, b;
		Basis(toLight, t, b);
		const uint rays = u_Push.Counts.z;
		const float cosMax = cos(u_Push.SkyRadiance.w);
		float visible = 0.0;
		for (uint k = 0u; k < rays; k++)
		{
			const float cosT = 1.0 - Rand() * (1.0 - cosMax);
			const float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
			const float phi = 2.0 * PI * Rand();
			const vec3 L = normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + toLight * cosT);
			if (!Occluded(origin + L * 1e-3, L, distanceLimit))
				visible += 1.0;
		}
		sunVisibility = visible / float(rays);
	}

	float aoVisibility = 1.0;
	const uint aoRays = u_Push.Counts.w;
	if (aoRays > 0u)
	{
		const float radius = u_Push.GroundRadiance.y;
		vec3 t, b;
		Basis(N, t, b);
		float visible = 0.0;
		for (uint k = 0u; k < aoRays; k++)
		{
			const float squaredRadius = Rand();
			const float phi = 2.0 * PI * (float(k) + Rand()) / float(aoRays);
			const float r = sqrt(squaredRadius);
			const vec3 L = t * (r * cos(phi)) + b * (r * sin(phi)) + N * sqrt(1.0 - squaredRadius);
			visible += LocalAmbientVisibility(origin, L, radius);
		}
		aoVisibility = visible / float(aoRays);
	}
	imageStore(o_RSMask, pixel, vec4(vec3(sunVisibility), aoVisibility));
}
