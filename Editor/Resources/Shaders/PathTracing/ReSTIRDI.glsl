// ----------------------------------------------------
// -- Lux ReSTIR DI: resampled direct lighting       --
// ----------------------------------------------------
//
// Reservoir-based spatiotemporal importance resampling of direct light (Bitterli et al. 2020), fused into one pass. Per pixel:
//   1. find the visible surface with a ray (no G-buffer needed) and write it to this frame's G-buffer;
//   2. draw a few candidate light samples (light chosen by the power-weighted alias table, placed by two random numbers) and keep one
//      by streaming resampling, with the unshadowed contribution as the target function;
//   3. merge the previous frame's reservoirs at the reprojected pixel and a few neighbours of it (after normal and position
//      tests), re-evaluating their light samples at this surface;
//   4. trace ONE shadow ray for the winner (a shadowed winner keeps weight 0, so occluded samples are not reused),
//      shade, and store the reservoir for the next frame.
// The reuse uses the simple 1 / M normalisation (the paper's biased variant): it slightly darkens shadow edges. The path tracer
// uses the result in place of its first-hit next-event estimation (SceneRendererOptions::PathTracerReSTIRDI). See docs/GI_PLAN.md.
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

layout(rgba32f, set = 1, binding = 100) uniform writeonly image2D o_RDResA;      // light index, u1, u2, M
layout(rgba32f, set = 1, binding = 101) uniform writeonly image2D o_RDResB;      // x = unbiased contribution weight W
layout(rgba32f, set = 1, binding = 102) uniform writeonly image2D o_RDGPos;      // xyz world position, w = 1 when a surface was hit
layout(rgba16f, set = 1, binding = 103) uniform writeonly image2D o_RDGNormal;   // xyz world normal
layout(rgba16f, set = 1, binding = 104) uniform writeonly image2D o_RDDirect;    // outgoing direct radiance at the visible surface
layout(set = 1, binding = 105) uniform texture2D u_RDPrevResA;
layout(set = 1, binding = 106) uniform texture2D u_RDPrevResB;
layout(set = 1, binding = 107) uniform texture2D u_RDPrevGPos;
layout(set = 1, binding = 108) uniform texture2D u_RDPrevGNormal;

const float MAX_HISTORY = 32.0;
const uint CANDIDATES_PER_ROUND = 8u;
const uint PRESAMPLED_ROUNDS = 4u;
shared uint s_CandidateLights[CANDIDATES_PER_ROUND * PRESAMPLED_ROUNDS];

float Lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Unshadowed contribution of light sample (index, u1, u2) at surface s, as luminance (the RIS target function).
float TargetPdf(Surface s, vec3 V, uint index, float u1, float u2)
{
	vec3 L, radiance;
	float dist, solidFactor;
	if (!SampleLight(index, u1, u2, s.P, s.N, L, dist, radiance, solidFactor))
		return 0.0;
	return Lum(EvalBRDF(s, V, L) * radiance);
}

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	const uvec2 size = u_Push.SizeFrameBounces.xy;
	const uint frame = u_Push.SizeFrameBounces.z;
	const bool presample = u_Push.Counts.x >= 64u;
	if (presample)
	{
		// Share only light indices. Each pixel picks its own column, emitter position and reservoir winner.
		// Keep the barrier before the bounds check so partial workgroups participate.
		const uint lane = gl_LocalInvocationIndex;
		if (lane < CANDIDATES_PER_ROUND * PRESAMPLED_ROUNDS)
		{
			const uint tile = gl_WorkGroupID.x + gl_NumWorkGroups.x * gl_WorkGroupID.y;
			const uint randomBits = PCG(lane + PCG(tile + PCG(frame * 5219u + 19u)));
			s_CandidateLights[lane] = ChooseLight(float(randomBits >> 8u) * (1.0 / 16777216.0));
		}
		barrier();
	}
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 7919u + 3u)));
	const bool historyValid = u_Push.Counts.z != 0u;
	const uint candidates = max(u_Push.Counts.w, 1u);

	vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
	vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	vec3 dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);

	g_InitialConeAngle = 2.0 / (u_Camera.ProjectionMatrix[1][1] * float(size.y));
	g_ConeWidth = 0.0;
	g_ConeAngle = g_InitialConeAngle;
	Surface s;
	if (u_Push.Counts.x == 0u || (u_Push.Counts.y & FLAG_UNIFORM_ENV) != 0u || !TraceClosest(origin, dir, s))
	{
		imageStore(o_RDResA, ivec2(pixel), vec4(0.0));
		imageStore(o_RDResB, ivec2(pixel), vec4(0.0));
		imageStore(o_RDGPos, ivec2(pixel), vec4(0.0));
		imageStore(o_RDGNormal, ivec2(pixel), vec4(0.0));
		imageStore(o_RDDirect, ivec2(pixel), vec4(0.0));
		return;
	}
	const vec3 V = -dir;
	const float viewDistance = length(s.P - origin);

	// 1. initial candidates
	uint selectedIndex = 0u;
	float selectedU1 = 0.0, selectedU2 = 0.0, selectedTarget = 0.0;
	float weightSum = 0.0, count = 0.0;
	for (uint i = 0u; i < candidates; i++)
	{
		const float choice = Rand();
		const uint column = min(uint(choice * float(CANDIDATES_PER_ROUND)), CANDIDATES_PER_ROUND - 1u);
		const uint index = presample && i < PRESAMPLED_ROUNDS
			? s_CandidateLights[i * CANDIDATES_PER_ROUND + column]
			: ChooseLight(choice);
		const float u1 = Rand(), u2 = Rand();
		const float target = TargetPdf(s, V, index, u1, u2);
		const float weight = target / LightPmf(index);
		weightSum += weight;
		count += 1.0;
		if (Rand() * weightSum < weight)
		{
			selectedIndex = index;
			selectedU1 = u1;
			selectedU2 = u2;
			selectedTarget = target;
		}
	}

	// 2. previous frame's reservoirs at the reprojected pixel and a few neighbours
	if (historyValid)
	{
		vec4 prevClip = u_Camera.PreviousViewProjectionMatrix * vec4(s.P, 1.0);
		vec2 prevNdc = prevClip.xy / prevClip.w;
		ivec2 prevPixel = ivec2(vec2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5) * vec2(size));
		for (uint tap = 0u; tap < 4u; tap++)
		{
			ivec2 q = prevPixel;
			if (tap > 0u)
				q += ivec2(round((vec2(Rand(), Rand()) * 2.0 - 1.0) * 12.0));
			if (q.x < 0 || q.y < 0 || q.x >= int(size.x) || q.y >= int(size.y))
				continue;
			const vec4 prevPos = texelFetch(u_RDPrevGPos, q, 0);
			if (prevPos.w < 0.5)
				continue;
			if (dot(texelFetch(u_RDPrevGNormal, q, 0).xyz, s.N) < 0.9 || distance(prevPos.xyz, s.P) > 0.05 * viewDistance + 0.02)
				continue;
			const vec4 a = texelFetch(u_RDPrevResA, q, 0);
			const float prevCount = min(a.w, MAX_HISTORY);
			if (prevCount <= 0.0)
				continue;
			const float prevW = texelFetch(u_RDPrevResB, q, 0).x;
			const uint index = uint(a.x + 0.5);
			const float target = TargetPdf(s, V, index, a.y, a.z);
			const float weight = target * prevW * prevCount;
			weightSum += weight;
			count += prevCount;
			if (Rand() * weightSum < weight)
			{
				selectedIndex = index;
				selectedU1 = a.y;
				selectedU2 = a.z;
				selectedTarget = target;
			}
		}
	}

	// 3. unbiased contribution weight, one shadow ray, shade
	float W = selectedTarget > 0.0 ? weightSum / (count * selectedTarget) : 0.0;
	vec3 color = vec3(0.0);
	if (W > 0.0)
	{
		vec3 L, radiance;
		float dist, solidFactor;
		vec3 shadow = vec3(0.0);
		if (SampleLight(selectedIndex, selectedU1, selectedU2, s.P, s.N, L, dist, radiance, solidFactor) && (shadow = ShadowTransmittance(s.P + s.N * 1e-3, L, dist - 2e-3), max(shadow.r, max(shadow.g, shadow.b)) > 0.0))
			color = EvalBRDF(s, V, L) * radiance * W * FogToLight(s.P, L, dist) * shadow;
		else
			W = 0.0;
	}

	imageStore(o_RDResA, ivec2(pixel), vec4(float(selectedIndex), selectedU1, selectedU2, min(count, MAX_HISTORY)));
	imageStore(o_RDResB, ivec2(pixel), vec4(W, 0.0, 0.0, 0.0));
	imageStore(o_RDGPos, ivec2(pixel), vec4(s.P, 1.0));
	imageStore(o_RDGNormal, ivec2(pixel), vec4(s.N, 0.0));
	imageStore(o_RDDirect, ivec2(pixel), vec4(color, 1.0));
}
