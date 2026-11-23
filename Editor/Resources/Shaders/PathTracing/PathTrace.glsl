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

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if (pixel.x >= u_Push.SizeFrameBounces.x || pixel.y >= u_Push.SizeFrameBounces.y)
		return;

	const uint frame = u_Push.SizeFrameBounces.z;
	g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 9781u + 1u)));

	// Primary ray with a box-filtered sub-pixel jitter.
	vec2 uv = (vec2(pixel) + vec2(Rand(), Rand())) / vec2(u_Push.SizeFrameBounces.xy);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
	vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	vec3 dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);

	vec3 radiance = TracePath(origin, dir, u_Push.SizeFrameBounces.w);

	// Firefly clamp (a documented bias): single samples above this are scaled down, preserving hue.
	const float maxSample = 64.0;
	float peak = max(radiance.r, max(radiance.g, radiance.b));
	if (peak > maxSample)
		radiance *= maxSample / peak;

	vec3 previous = frame == 0u ? vec3(0.0) : imageLoad(o_PathTraceAccum, ivec2(pixel)).rgb;
	vec3 mean = (previous * float(frame) + radiance) / float(frame + 1u);
	imageStore(o_PathTraceAccum, ivec2(pixel), vec4(mean, 1.0));
	imageStore(o_PathTraceImage, ivec2(pixel), vec4(mean, 1.0));
}
