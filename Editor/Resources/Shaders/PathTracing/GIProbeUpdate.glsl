// ----------------------------------------------------
// -- Lux realtime GI: probe volume update (RT-only) --
// ----------------------------------------------------
//
// A regular grid of probes over the scene bounds. Each frame every probe traces a few rays on a rotated
// Fibonacci sphere with the shared path kernel (direct light by next-event estimation at every hit, plus
// bounces), projects the incoming radiance onto L1 spherical harmonics and blends it into its history
// (hysteresis: lighting changes show up over a few frames; the temporal blend never touches the camera, so
// camera motion cannot ghost). GBuffer_Static.glsl reads the grid as indirect diffuse. Probes have no
// visibility term, so light leaks through thin walls (documented limit). See docs/GI_PLAN.md, GI4.
//
// GIProbes layout (vec4 elements): [0] = (grid origin, 1 when the grid is valid), [1] = (cell size, 0),
// [2] = (dims, 0) as floats, then 4 vec4 per probe: SH L1 coefficients (r,g,b in xyz) for Y00, Y1-1, Y10, Y11.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include <PathTraceCommon.glslh>

layout(std430, set = 2, binding = 73) buffer GIProbes { vec4 u_GIProbes[]; };

const float Y0 = 0.282095;
const float Y1 = 0.488603;

void main()
{
	const uint index = gl_GlobalInvocationID.x;
	if (index >= u_Push.SizeFrameBounces.x)
		return;

	const vec3 origin = u_GIProbes[0].xyz;
	const vec3 cell = u_GIProbes[1].xyz;
	const uvec3 dims = uvec3(u_GIProbes[2].xyz);
	const uvec3 coord = uvec3(index % dims.x, (index / dims.x) % dims.y, index / (dims.x * dims.y));
	const vec3 position = origin + vec3(coord) * cell;

	const uint frame = u_Push.SizeFrameBounces.z;
	const uint rayCount = u_Push.SizeFrameBounces.y;
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
	}
	const float weight = 4.0 * PI / float(rayCount);
	sh0 *= weight; sh1 *= weight; sh2 *= weight; sh3 *= weight;

	// Hysteresis: the first frames after a change take the new estimate outright.
	const float alpha = frame < 4u ? 1.0 : 0.06;
	const uint base = 3u + index * 4u;
	u_GIProbes[base + 0u] = vec4(mix(u_GIProbes[base + 0u].xyz, sh0, alpha), 0.0);
	u_GIProbes[base + 1u] = vec4(mix(u_GIProbes[base + 1u].xyz, sh1, alpha), 0.0);
	u_GIProbes[base + 2u] = vec4(mix(u_GIProbes[base + 2u].xyz, sh2, alpha), 0.0);
	u_GIProbes[base + 3u] = vec4(mix(u_GIProbes[base + 3u].xyz, sh3, alpha), 0.0);
}
