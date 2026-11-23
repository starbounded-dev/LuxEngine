// --------------------------------------------------
// -- Lux ray-traced GI: per-pixel indirect trace  --
// --------------------------------------------------
//
// One invocation per pixel. A primary ray finds the visible surface in the ray-traced scene (so no G-buffer
// decode is needed), then ONE cosine-weighted indirect path (direct light by next-event estimation at its
// hit, plus bounces) estimates the mean incident radiance (irradiance / pi). The estimate is blended into a
// per-pixel history (an exponential mean that restarts whenever the camera, lights or scene change, so it
// cannot ghost), then RTGIFilter.glsl denoises it and adds albedo * (1 - metalness) * it to scene colour
// as indirect diffuse. See docs/GI_PLAN.md, GI5.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require

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

layout(rgba16f, set = 1, binding = 90) uniform image2D o_RTGIAccum;                // rgb mean incident radiance, a = 1 where a surface was hit
layout(rgba16f, set = 1, binding = 91) uniform writeonly image2D o_RTGIAlbedo;     // rgb albedo * (1 - metalness), a = hit distance
layout(rgba16f, set = 1, binding = 92) uniform writeonly image2D o_RTGINormal;     // xyz world normal

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if (pixel.x >= u_Push.SizeFrameBounces.x || pixel.y >= u_Push.SizeFrameBounces.y)
		return;

	const uint frame = u_Push.SizeFrameBounces.z;
	g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 6151u + 11u)));

	// Pixel centre (a stable surface per pixel; the noise is in the indirect sample, not the geometry).
	vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(u_Push.SizeFrameBounces.xy);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
	vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	vec3 dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);

	Surface s;
	if (!TraceClosest(origin, dir, s))
	{
		imageStore(o_RTGIAccum, ivec2(pixel), vec4(0.0));
		imageStore(o_RTGIAlbedo, ivec2(pixel), vec4(0.0));
		imageStore(o_RTGINormal, ivec2(pixel), vec4(0.0));
		return;
	}

	vec3 t, b;
	Basis(s.N, t, b);
	float u1 = Rand(), u2 = Rand();
	float r = sqrt(u1);
	float phi = 2.0 * PI * u2;
	vec3 sampleDir = normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + s.N * sqrt(max(1.0 - u1, 0.0)));

	vec3 incident = TracePath(s.P + s.N * 2e-3, sampleDir, u_Push.SizeFrameBounces.w);
	float peak = max(incident.r, max(incident.g, incident.b));
	if (peak > 16.0)
		incident *= 16.0 / peak;

	// Running mean that settles to a 1/32 exponential average, restarting at frame 0.
	vec3 history = imageLoad(o_RTGIAccum, ivec2(pixel)).rgb;
	float alpha = frame == 0u ? 1.0 : max(1.0 / float(frame + 1u), 1.0 / 32.0);
	imageStore(o_RTGIAccum, ivec2(pixel), vec4(mix(history, incident, alpha), 1.0));
	imageStore(o_RTGIAlbedo, ivec2(pixel), vec4(s.Albedo * (1.0 - s.Metal), length(s.P - origin)));
	imageStore(o_RTGINormal, ivec2(pixel), vec4(s.N, 0.0));
}
