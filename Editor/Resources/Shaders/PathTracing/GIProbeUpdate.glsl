// ----------------------------------------------------
// -- Lux realtime GI: probe volume update (RT-only) --
// ----------------------------------------------------
//
// A regular grid of probes over the scene bounds. Each frame every probe traces a few rays on a rotated
// Fibonacci sphere with the shared path kernel (direct light by next-event estimation at every hit, plus
// bounces), projects the incoming radiance onto L1 spherical harmonics and blends it into its history
// (hysteresis: lighting changes show up over a few frames; the temporal blend never touches the camera, so
// camera motion cannot ghost). GBuffer_Static.glsl reads the grid as indirect diffuse.
//
// Visibility (GIL5): besides radiance each probe keeps an 8x8 octahedral map of the mean distance and mean squared
// distance to the nearest surface per direction, and the share of rays that hit a surface from behind (the probe sits
// inside geometry). The G-buffer pass weights probes with them (Chebyshev, as DDGI) so light does not leak through
// walls.
//
// GIProbes layout (vec4 elements): [0] = (grid origin, 1 when the grid is valid), [1] = (cell size, 0),
// [2] = (dims, 0) as floats, then 36 vec4 per probe: [0..3] SH L1 radiance (r,g,b in xyz; w of the first = backface
// share) for Y00, Y1-1, Y10, Y11; [4..35] the distance map, 64 texels (texel t at element 4 + t/2, xy for even t, zw for
// odd t) holding (mean distance, mean squared distance).
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include <PathTraceCommon.glslh>

layout(std430, set = 2, binding = 73) buffer GIProbes { vec4 u_GIProbes[]; };
#include <GIProbeGrid.glslh>

const float Y0 = 0.282095;
const float Y1 = 0.488603;
const uint MAX_RAYS = 128u;
const int MAP_SIZE = 8;
const uint GI_AUTO_REFRESH = 1u;
const uint GI_MEASURE_ERROR = 2u;
const uint GI_USE_PLACEMENT = 4u;
const uint GI_BUILD_PLACEMENT = 8u;

vec2 SignNotZero(vec2 v) { return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }

vec3 OctDecode(vec2 uv)
{
	vec2 e = uv * 2.0 - 1.0;
	vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
		n.xy = (1.0 - abs(n.yx)) * SignNotZero(n.xy);
	return normalize(n);
}

// A plane pair shares one spacing interval along its axis. Measure radiance variation across
// the planes and changes observed at the probes while warming up on the uniform lattice.
float ProbeIntervalError(uint interval, uint axis, uvec3 dims)
{
	const uint first = (axis + 1u) % 3u;
	const uint second = (axis + 2u) % 3u;
	uvec3 coord = uvec3(0u);
	coord[axis] = interval;
	uvec3 next = uvec3(0u);
	next[axis] = 1u;
	float error = 0.0;
	float count = 0.0;
	for (uint row = 0u; row < dims[first]; row++)
	{
		coord[first] = row;
		for (uint column = 0u; column < dims[second]; column++)
		{
			coord[second] = column;
			const uint a = GI_PROBE_HEADER + GI_PROBE_STRIDE * GIProbeIndex(coord, dims);
			const uint b = GI_PROBE_HEADER + GI_PROBE_STRIDE * GIProbeIndex(coord + next, dims);
			const vec4 shA = u_GIProbes[a];
			const vec4 shB = u_GIProbes[b];
			if (max(shA.w, shB.w) > 0.8)
				continue;
			const float variance = max(u_GIProbes[a + 1u].w, u_GIProbes[b + 1u].w);
			error += sqrt(max(variance, 0.0)) + length(shB.xyz - shA.xyz);
			count += 1.0;
		}
	}
	return error / max(count, 1.0);
}

void PlaceProbeAxis(uint axis, uvec3 dims)
{
	const uint placement = GIPlacementBase(dims);
	const uint start = GIPlacementAxis(placement, dims, axis);
	const uint intervals = dims[axis] - 1u;
	float errors[GI_MAX_AXIS_SIZE - 1u];
	float totalError = 0.0;
	for (uint i = 0u; i < intervals; i++)
	{
		errors[i] = ProbeIntervalError(i, axis, dims);
		totalError += errors[i];
	}
	const float meanError = totalError / float(intervals);
	float cumulative[GI_MAX_AXIS_SIZE];
	cumulative[0] = 0.0;
	for (uint i = 0u; i < intervals; i++)
	{
		// A uniform part keeps all regions covered. The cap limits what one noisy plane can consume.
		const float density = 1.0 + min(errors[i] / max(meanError, 1e-6), 4.0);
		cumulative[i + 1u] = cumulative[i] + density;
	}
	uint interval = 0u;
	u_GIProbes[start] = vec4(0.0);
	for (uint i = 1u; i < intervals; i++)
	{
		const float target = cumulative[intervals] * (float(i) / float(intervals));
		while (interval + 1u < intervals && cumulative[interval + 1u] < target)
			interval++;
		const float fraction = (target - cumulative[interval]) / (cumulative[interval + 1u] - cumulative[interval]);
		u_GIProbes[start + i] = vec4(float(interval) + fraction, 0.0, 0.0, 0.0);
	}
	u_GIProbes[start + intervals] = vec4(float(intervals), 0.0, 0.0, 0.0);
	if (axis == 0u)
		u_GIProbes[placement] = vec4(1.0, 0.0, 0.0, 0.0);
}

void main()
{
	const uint index = gl_GlobalInvocationID.x;
	const uint placementFlags = u_Push.Counts.w;
	const uvec3 dims = uvec3(u_GIProbes[2].xyz);
	if ((placementFlags & GI_BUILD_PLACEMENT) != 0u)
	{
		if (index < 3u)
			PlaceProbeAxis(index, dims);
		return;
	}
	if (index >= u_Push.SizeFrameBounces.x)
		return;

	const vec3 origin = u_GIProbes[0].xyz;
	const vec3 cell = u_GIProbes[1].xyz;
	const uvec3 coord = uvec3(index % dims.x, (index / dims.x) % dims.y, index / (dims.x * dims.y));
	const uint placement = (placementFlags & GI_USE_PLACEMENT) != 0u ? GIPlacementBase(dims) : 0u;
	const vec3 position = GIProbePosition(origin, cell, coord, dims, placement);

	const uint frame = u_Push.SizeFrameBounces.z;
	const bool measureError = (placementFlags & GI_MEASURE_ERROR) != 0u;
	if (measureError && frame == 0u && index == 0u)
		u_GIProbes[GIPlacementBase(dims)] = vec4(0.0);
	// Auto placement: a probe found inside solid geometry on an earlier update (most of its rays hit back faces) is refreshed every 16th frame only.
	if ((placementFlags & GI_AUTO_REFRESH) != 0u && frame > 8u && u_GIProbes[GI_PROBE_HEADER + index * GI_PROBE_STRIDE].w > 0.8 && (frame & 15u) != 0u)
		return;
	const uint rayCount = min(u_Push.SizeFrameBounces.y, MAX_RAYS);
	g_Rng = PCG(index + PCG(frame * 7919u + 3u));

	// Random rotation of the sphere pattern per probe per frame.
	vec3 axisU = normalize(vec3(Rand() - 0.5, Rand() - 0.5, Rand() - 0.5) + vec3(1e-3));
	vec3 axisT, axisB;
	Basis(axisU, axisT, axisB);
	float spin = Rand() * 2.0 * PI;

	vec3 sh0 = vec3(0.0);
	vec3 sh1 = vec3(0.0);
	vec3 sh2 = vec3(0.0);
	vec3 sh3 = vec3(0.0);
	float backfaces = 0.0;
	vec3 rayDirections[MAX_RAYS];
	float rayDistances[MAX_RAYS];
	const float maxDistance = 2.0 * length(cell * vec3(dims));
	for (uint i = 0u; i < rayCount; i++)
	{
		float z = 1.0 - (2.0 * float(i) + 1.0) / float(rayCount);
		float r = sqrt(max(1.0 - z * z, 0.0));
		float phi = float(i) * 2.399963 + spin;
		vec3 local = vec3(r * cos(phi), r * sin(phi), z);
		vec3 dir = normalize(axisT * local.x + axisB * local.y + axisU * local.z);

		vec3 radiance = TracePath(position, dir, u_Push.SizeFrameBounces.w);
		float peak = max(radiance.r, max(radiance.g, radiance.b));
		if (peak > 32.0)
			radiance *= 32.0 / peak;

		sh0 += radiance * Y0;
		sh1 += radiance * (Y1 * dir.y);
		sh2 += radiance * (Y1 * dir.z);
		sh3 += radiance * (Y1 * dir.x);

		float hitT;
		bool back;
		TraceDistance(position, dir, maxDistance, hitT, back);
		backfaces += back ? 1.0 : 0.0;
		rayDirections[i] = dir;
		rayDistances[i] = hitT;
	}
	const float weight = 4.0 * PI / float(rayCount);
	sh0 *= weight; sh1 *= weight; sh2 *= weight; sh3 *= weight;
	const float backfaceShare = backfaces / float(rayCount);

	// Hysteresis: the first frames after a change take the new estimate outright.
	const float alpha = frame < 4u ? 1.0 : 0.06;
	const uint base = GI_PROBE_HEADER + index * GI_PROBE_STRIDE;
	float error = 0.0;
	if (measureError && frame > 0u)
	{
		const vec3 residual = sh0 - u_GIProbes[base].xyz;
		const float sampleError = dot(residual, residual);
		error = mix(u_GIProbes[base + 1u].w, sampleError, max(1.0 / float(frame), 0.08));
	}
	u_GIProbes[base + 0u] = vec4(mix(u_GIProbes[base + 0u].xyz, sh0, alpha), mix(u_GIProbes[base + 0u].w, backfaceShare, alpha));
	u_GIProbes[base + 1u] = vec4(mix(u_GIProbes[base + 1u].xyz, sh1, alpha), error);
	u_GIProbes[base + 2u] = vec4(mix(u_GIProbes[base + 2u].xyz, sh2, alpha), 0.0);
	u_GIProbes[base + 3u] = vec4(mix(u_GIProbes[base + 3u].xyz, sh3, alpha), 0.0);

	// Octahedral distance map: each texel gathers the rays near its direction.
	for (uint pair = 0u; pair < 32u; pair++)
	{
		vec4 updated = vec4(0.0);
		for (uint lane = 0u; lane < 2u; lane++)
		{
			uint texel = pair * 2u + lane;
			vec2 uv = (vec2(float(texel % 8u), float(texel / 8u)) + 0.5) / float(MAP_SIZE);
			vec3 texelDirection = OctDecode(uv);
			float sum1 = 0.0, sum2 = 0.0, sumWeight = 0.0;
			for (uint i = 0u; i < rayCount; i++)
			{
				float w = pow(max(dot(texelDirection, rayDirections[i]), 0.0), 24.0);
				sum1 += w * rayDistances[i];
				sum2 += w * rayDistances[i] * rayDistances[i];
				sumWeight += w;
			}
			vec2 value = sumWeight > 1e-4 ? vec2(sum1, sum2) / sumWeight : vec2(maxDistance, maxDistance * maxDistance);
			if (lane == 0u)
				updated.xy = value;
			else
				updated.zw = value;
		}
		u_GIProbes[base + 4u + pair] = mix(u_GIProbes[base + 4u + pair], updated, alpha);
	}
}
