// ----------------------------------------------------
// -- Lux ray-traced reflections (raster frame)      --
// ----------------------------------------------------
//
// Runs after the raster lighting. For every pixel the raster depth and G-buffer give the surface (so the pass lines up with raster
// pixels: cutouts, particles and transparents are not mis-traced); a smooth enough surface traces GGX-sampled reflection rays in the
// ray-traced scene, shades what they hit (emission + direct light by next-event estimation) and adds F * radiance to scene colour.
// Rays that escape add nothing: the sky is already in the frame as the raster environment reflection. A ray that hits geometry
// replaces it: the radiance the environment would have given along that ray is subtracted (flag 128), so a bright sky is not
// counted twice. The GGX lobe broadens with roughness. Mirrors trace once, and the optional adaptive budget tapers toward the
// roughness cutoff. When the denoiser is active, rough pixels store the signed correction for filtering before compositing.
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

layout(set = 1, binding = 150) uniform texture2D u_RRDepth;        // device depth (reverse-Z: 0 = sky)
layout(set = 1, binding = 151) uniform texture2D u_RRNormal;       // view-space normal
layout(set = 1, binding = 152) uniform texture2D u_RRMetalRough;   // x metalness, y roughness
layout(set = 1, binding = 153) uniform texture2D u_RRBaseColor;
layout(set = 1, binding = 154) uniform texture2D u_RRSceneColor;
layout(rgba16f, set = 1, binding = 155) uniform writeonly image2D o_RRResult;

const uint RR_REPLACE_ENVIRONMENT = 1u << 7u;
const uint RR_ADAPTIVE_BUDGET = 1u << 8u;
const uint RR_DENOISE = 1u << 9u;
const float RR_MIRROR_THRESHOLD = 0.06;

uint ReflectionRayCount(float roughness, float cutoff)
{
	if (roughness < RR_MIRROR_THRESHOLD)
		return 1u;
	const uint maxRays = max(u_Push.Counts.z, 1u);
	if ((u_Push.Counts.y & RR_ADAPTIVE_BUDGET) == 0u)
		return maxRays;
	const uint minRays = (maxRays + 3u) / 4u;
	const float roughRange = max(cutoff - RR_MIRROR_THRESHOLD, 1e-3);
	const float taper = smoothstep(0.0, 1.0, (roughness - RR_MIRROR_THRESHOLD) / roughRange);
	return uint(round(mix(float(maxRays), float(minRays), taper)));
}

// Light leaving a hit toward the reflecting surface: every light gets its own sample when there are few (no light-choice noise in what a
// mirror shows), otherwise one light by the power-weighted alias table as the path tracer does.
vec3 ShadeHit(Surface s, vec3 V)
{
	const uint count = u_Push.Counts.x;
	if (count == 0u)
		return vec3(0.0);
	if (count > 6u)
		return DirectLight(s, V, true);
	vec3 sum = vec3(0.0);
	for (uint index = 0u; index < count; index++)
	{
		const float u1 = Rand(), u2 = Rand();
		vec3 L, radiance;
		float dist, solidFactor;
		if (!SampleLight(index, u1, u2, s.P, s.N, L, dist, radiance, solidFactor))
			continue;
		const vec3 shadow = ShadowTransmittance(s.P + s.N * 1e-3, L, dist - 2e-3);
		sum += EvalBRDF(s, V, L) * radiance * FogToLight(s.P, L, dist) * shadow;
	}
	return sum;
}

void main()
{
	const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Push.SizeFrameBounces.xy);
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	vec4 scene = texelFetch(u_RRSceneColor, pixel, 0);
	const float depth = texelFetch(u_RRDepth, pixel, 0).r;
	const vec2 metalRough = texelFetch(u_RRMetalRough, pixel, 0).xy;
	const float roughness = max(metalRough.y, 0.02);
	const float cutoff = uintBitsToFloat(u_Push.Counts.w);
	const bool denoise = (u_Push.Counts.y & RR_DENOISE) != 0u;
	if (depth <= 1e-6 || roughness > cutoff)
	{
		imageStore(o_RRResult, pixel, denoise ? vec4(scene.rgb, 0.0) : scene);
		return;
	}

	const vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);
	const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	const vec4 world = u_Camera.InverseViewProjectionMatrix * vec4(ndc, depth, 1.0);
	const vec3 P = world.xyz / world.w;
	const vec3 N = normalize(mat3(u_Camera.InverseViewMatrix) * texelFetch(u_RRNormal, pixel, 0).xyz);
	const vec3 base = texelFetch(u_RRBaseColor, pixel, 0).rgb;
	const vec3 V = normalize(u_Camera.IsOrthographic != 0 ? u_Camera.InverseViewMatrix[2].xyz : u_Camera.InverseViewMatrix[3].xyz - P);
	const vec3 F0 = mix(vec3(0.04), base, metalRough.x);
	const float alpha = max(roughness * roughness, MIN_ALPHA);

	g_Rng = PCG(uint(pixel.x) + PCG(uint(pixel.y) + PCG(u_Push.SizeFrameBounces.z * 7507u + 5u)));
	g_InitialConeAngle = 0.002;
	g_ConeWidth = 0.0;
	g_ConeAngle = 0.002;
	vec3 t, b;
	Basis(N, t, b);

	const uint rays = ReflectionRayCount(roughness, cutoff);
	const bool replaceEnvironment = (u_Push.Counts.y & RR_REPLACE_ENVIRONMENT) != 0u;
	vec3 sum = vec3(0.0);
	for (uint k = 0u; k < rays; k++)
	{
		const float u1 = Rand(), u2 = Rand();
		const float cosT = sqrt((1.0 - u1) / (1.0 + (alpha * alpha - 1.0) * u1));
		const float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
		const float phi = 2.0 * PI * u2;
		const vec3 H = normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + N * cosT);
		const vec3 R = reflect(-V, H);
		if (dot(R, N) <= 0.0)
			continue;
		Surface s;
		if (!TraceClosest(P + N * 2e-3, R, s))
			continue; // the sky: the raster environment reflection already has it
		const vec3 fresnel = F0 + (vec3(1.0) - F0) * pow(1.0 - max(dot(V, H), 0.0), 5.0);
		sum += fresnel * (s.Emission + ShadeHit(s, -R) - (replaceEnvironment ? Environment(R) : vec3(0.0)));
	}
	const vec3 correction = sum / float(rays);
	if (denoise && roughness >= RR_MIRROR_THRESHOLD)
	{
		imageStore(o_RRResult, pixel, vec4(correction, 1.0));
		return;
	}
	scene.rgb = max(scene.rgb + correction, vec3(0.0));
	if (denoise)
		scene.a = 0.0;
	imageStore(o_RRResult, pixel, scene);
}
