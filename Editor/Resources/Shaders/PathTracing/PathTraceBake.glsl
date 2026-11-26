// ------------------------------------------
// -- Lux lightmap baker (editor-only, GPU) --
// ------------------------------------------
//
// One invocation per lightmap texel. A texel stores a world position and normal (written by the CPU
// rasteriser of the mesh's lightmap UVs); each dispatch adds one cosine-weighted hemisphere path to a
// running mean, so after N dispatches the texel holds the mean incident radiance (irradiance / pi). That
// is *indirect* light only: rays leave the texel, and whatever they hit is shaded fully (direct light,
// emission, further bounces). The raster renderer keeps its own direct lighting and adds
// albedo * (1 - metalness) * this value as indirect diffuse (GBuffer_Static.glsl).
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include <PathTraceCommon.glslh>

struct LMTexel
{
	vec4 Position; // world position, w unused
	vec4 Normal;   // world normal, w = 1 for a texel covered by geometry
};

layout(std430, set = 1, binding = 80) readonly buffer LMTexels { LMTexel Texels[]; };
layout(std430, set = 1, binding = 81) buffer LMAccum { vec4 Accum[]; }; // rgb running mean, a unused

void main()
{
	const uint index = gl_GlobalInvocationID.x;
	if (index >= u_Push.SizeFrameBounces.x)
		return;

	const LMTexel texel = Texels[index];
	if (texel.Normal.w < 0.5)
		return;

	const uint frame = u_Push.SizeFrameBounces.z;
	g_Rng = PCG(index + PCG(frame * 9781u + 7u));

	vec3 n = normalize(texel.Normal.xyz);
	vec3 t, b;
	Basis(n, t, b);
	float u1 = Rand(), u2 = Rand();
	float r = sqrt(u1);
	float phi = 2.0 * PI * u2;
	vec3 dir = normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u1, 0.0)));

	vec3 incident = TracePath(texel.Position.xyz + n * 2e-3, dir, u_Push.SizeFrameBounces.w);
	float peak = max(incident.r, max(incident.g, incident.b));
	if (peak > 64.0)
		incident *= 64.0 / peak;

	vec3 previous = frame == 0u ? vec3(0.0) : Accum[index].rgb;
	Accum[index] = vec4((previous * float(frame) + incident) / float(frame + 1u), 1.0);
}
