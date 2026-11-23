// -------------------------------------------------
// -- Lux path tracer (editor reference renderer) --
// -------------------------------------------------
//
// Unidirectional path tracer over a TLAS using ray queries (GL_EXT_ray_query), one pixel per
// invocation, accumulating a running mean in an RGBA32F image and writing the HDR result to an
// RGBA16F image the renderer copies into scene colour. Opt-in (SceneRendererOptions::EnablePathTracer);
// editor-only; see docs/GI_PLAN.md.
//
// Lighting: next-event estimation to one light per bounce (directional, point, spot, rect area),
// BSDF-sampled continuation (cosine diffuse + GGX specular mixture), constant sky/ground environment on
// escape. BRDF terms follow PBR.glslh (GGX + correlated Smith + Frostbite diffuse) so a path-traced
// frame and the raster frame agree on direct lighting. Textures are not sampled: albedo is the
// material's constant colour (a documented v1 limit).
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <PathTraceCommon.glslh>

// Identical to the Camera block in Buffers.glslh / ClusterBuild.glsl (same (set,binding), same name).
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

layout(rgba32f, set = 1, binding = 65) uniform image2D o_PathTraceAccum;
layout(rgba16f, set = 1, binding = 66) uniform writeonly image2D o_PathTraceImage;
layout(set = 1, binding = 109) uniform texture2D u_ReSTIRDirect; // first-hit direct light from ReSTIR DI (read only when FLAG_RESTIR_DI is set)
layout(rgba32f, set = 1, binding = 98) uniform image2D o_PathTraceMoments; // x = sum of luminance, y = sum of luminance^2 (adaptive sampling)

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if (pixel.x >= u_Push.SizeFrameBounces.x || pixel.y >= u_Push.SizeFrameBounces.y)
		return;

	const uint frame = u_Push.SizeFrameBounces.z;
	g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 9781u + 1u)));

	// Per-pixel sample count n (the accumulation alpha) and luminance moments. With adaptive sampling a pixel whose relative
	// standard error is already below the threshold is left alone; otherwise every pixel has n == frame.
	float n = 0.0;
	vec3 previous = vec3(0.0);
	vec2 moments = vec2(0.0);
	if (frame > 0u)
	{
		const vec4 accumulated = imageLoad(o_PathTraceAccum, ivec2(pixel));
		previous = accumulated.rgb;
		n = accumulated.a;
		moments = imageLoad(o_PathTraceMoments, ivec2(pixel)).xy;
	}
	const float adaptiveThreshold = uintBitsToFloat(u_Push.Counts.z);
	if (adaptiveThreshold > 0.0 && n >= 16.0)
	{
		const float meanLuminance = moments.x / n;
		const float variance = max(moments.y / n - meanLuminance * meanLuminance, 0.0);
		if (sqrt(variance / n) / max(meanLuminance, 0.02) < adaptiveThreshold)
			return;
	}

	// Primary ray with a box-filtered sub-pixel jitter.
	// ReSTIR DI shades the pixel-centre surface, so that mode traces from the centre too.
	const bool restir = (u_Push.Counts.y & FLAG_RESTIR_DI) != 0u;
	vec2 uv = (vec2(pixel) + (restir ? vec2(0.5) : vec2(Rand(), Rand()))) / vec2(u_Push.SizeFrameBounces.xy);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
	vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	vec3 dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);

	g_InitialConeAngle = 2.0 / (u_Camera.ProjectionMatrix[1][1] * float(u_Push.SizeFrameBounces.y));
	if (restir)
	{
		g_UseExternalDirect = true;
		g_ExternalDirect = texelFetch(u_ReSTIRDirect, ivec2(pixel), 0).rgb;
	}
	vec3 radiance = TracePath(origin, dir, u_Push.SizeFrameBounces.w);

	// Firefly clamp (a documented bias): single samples above this are scaled down, preserving hue.
	const float maxSample = 64.0;
	float peak = max(radiance.r, max(radiance.g, radiance.b));
	if (peak > maxSample)
		radiance *= maxSample / peak;

	const float luminance = dot(radiance, vec3(0.2126, 0.7152, 0.0722));
	vec3 mean = (previous * n + radiance) / (n + 1.0);
	imageStore(o_PathTraceAccum, ivec2(pixel), vec4(mean, n + 1.0));
	imageStore(o_PathTraceMoments, ivec2(pixel), vec4(moments + vec2(luminance, luminance * luminance), 0.0, 0.0));
	const float heat = (n + 1.0) / max(float(u_Push.Counts.w), 1.0);
	imageStore(o_PathTraceImage, ivec2(pixel), u_Push.Counts.w != 0u ? vec4(vec3(heat), 1.0) : vec4(mean, 1.0));
}
