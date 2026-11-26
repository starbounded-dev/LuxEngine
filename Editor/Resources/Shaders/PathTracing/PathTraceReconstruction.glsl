#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define LUX_PATH_TRACE_RECONSTRUCTION
#include <PathTracePixel.glslh>

layout(rg16f,   set = 1, binding = 186) uniform writeonly image2D o_RRMotion;
layout(r32f,    set = 1, binding = 187) uniform writeonly image2D o_RRViewDepth;
layout(rgba16f, set = 1, binding = 188) uniform writeonly image2D o_RRNormals;
layout(rgba16f, set = 1, binding = 189) uniform writeonly image2D o_RRDiffuseAlbedo;
layout(rgba16f, set = 1, binding = 190) uniform writeonly image2D o_RRSpecularAlbedo;
layout(r32f,    set = 1, binding = 191) uniform writeonly image2D o_RRSpecularHitDistance;

float ReflectedHitDistance(vec3 origin, vec3 direction)
{
	rayQueryEXT ray;
	rayQueryInitializeEXT(ray, u_RTScene, gl_RayFlagsOpaqueEXT, 0xff, origin, 1e-4, direction, 65504.0);
	while (rayQueryProceedEXT(ray)) {}
	if (rayQueryGetIntersectionTypeEXT(ray, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
		return rayQueryGetIntersectionTEXT(ray, true);
	return 65504.0;
}

vec3 SpecularGuide(Surface surface, vec3 viewDirection)
{
	// Split-sum environment BRDF with the same analytic fit as LuxEnvBRDFApprox.
	float NoV = max(dot(surface.N, viewDirection), 0.0);
	vec4 r = surface.Rough * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	vec2 scaleBias = vec2(-1.04, 1.04) * a004 + r.zw;
	return clamp(surface.F0 * scaleBias.x + scaleBias.y, vec3(0.0), vec3(1.0));
}

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	const uvec2 size = u_Push.SizeFrameBounces.xy;
	if (any(greaterThanEqual(pixel, size)))
		return;

	const uint frame = u_Push.SizeFrameBounces.z;
	g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 9781u + 1u)));
	const vec2 jitter = vec2(uintBitsToFloat(u_Push.Counts.z), uintBitsToFloat(u_Push.Counts.w));
	const vec2 uv = (vec2(pixel) + 0.5 + jitter) / vec2(size);
	const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	const vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	const vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	const vec3 direction = normalize((u_Camera.InverseViewMatrix * vec4(normalize(viewPoint.xyz / viewPoint.w), 0.0)).xyz);
	g_InitialConeAngle = 2.0 / (u_Camera.ProjectionMatrix[1][1] * float(size.y));

	vec3 radiance = TracePath(origin, direction, u_Push.SizeFrameBounces.w);
	const float peak = max(radiance.r, max(radiance.g, radiance.b));
	radiance *= min(1.0, 64.0 / max(peak, 1e-6));
	const ivec2 p = ivec2(pixel);
	imageStore(o_PathTraceImage, p, vec4(radiance, 1.0));

	// Use the same primary ray for all guides. The sky's motion is only rotational.
	// Reset the ray cone before shading the first surface after the illumination path.
	g_ConeWidth = 0.0;
	g_ConeAngle = g_InitialConeAngle;
	Surface surface;
	const bool hit = TraceClosest(origin, direction, surface);
	const vec4 previousClip = u_Camera.PreviousViewProjectionMatrix * (hit ? vec4(surface.P, 1.0) : vec4(direction, 0.0));
	const vec2 previousUV = previousClip.w > 0.0
		? previousClip.xy / previousClip.w * vec2(0.5, -0.5) + 0.5 : uv;
	imageStore(o_RRMotion, p, vec4(previousUV - uv, 0.0, 0.0));
	if (!hit)
	{
		imageStore(o_RRViewDepth, p, vec4(1.0e6));
		imageStore(o_RRNormals, p, vec4(-direction, 1.0));
		imageStore(o_RRDiffuseAlbedo, p, vec4(clamp(radiance, 0.0, 1.0), 1.0));
		imageStore(o_RRSpecularAlbedo, p, vec4(0.0));
		imageStore(o_RRSpecularHitDistance, p, vec4(65504.0));
		return;
	}

	float viewDepth = -(u_Camera.ViewMatrix * vec4(surface.P, 1.0)).z;
	float distance = ReflectedHitDistance(surface.P + surface.N * 1e-3, reflect(direction, surface.N));
	imageStore(o_RRViewDepth, p, vec4(viewDepth));
	imageStore(o_RRNormals, p, vec4(surface.N, surface.Rough));
	imageStore(o_RRDiffuseAlbedo, p, vec4(surface.Albedo * (1.0 - surface.Metal), 1.0));
	imageStore(o_RRSpecularAlbedo, p, vec4(SpecularGuide(surface, -direction), 1.0));
	imageStore(o_RRSpecularHitDistance, p, vec4(distance));
}
