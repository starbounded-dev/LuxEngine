// Soundwave listener reverb on the GPU: one invocation per ray, a line-by-line twin of
// Soundwave::TraceReverbRay (Core/vendor/soundwave/src/Reverb.cpp). Each ray writes its own
// partial sums; FinishReverb reduces them on the CPU in ray order (the fixed reduction order).

#version 450 core
#pragma stage : comp

#include <Soundwave.glslh>

#define SOUNDWAVE_SURFACE_MARGIN 0.001
#define SOUNDWAVE_ESCAPE_DISTANCE 200.0
#define SOUNDWAVE_GOLDEN_ANGLE 2.39996323

layout(std430, set = 1, binding = 33) readonly buffer SoundwaveReverbRequest
{
	vec4 Listener;  // xyz
	uvec4 Config;   // rays, bounces, root 0, root 1
	vec4 Params;    // x = absorption fraction
	vec4 Materials[SOUNDWAVE_MATERIALS]; // absorption LF, absorption HF, scattering
} b_Request;

struct SoundwaveReverbRay
{
	float Absorbed;
	float Travelled;
	uint Hits;
	uint Traces; // bit 31 = escaped
};
layout(std430, set = 1, binding = 34) writeonly buffer SoundwaveReverbRays { SoundwaveReverbRay Rays[]; } b_Rays;

vec3 Diffuse(vec3 normal, uint ray, uint bounce)
{
	uint h = (ray * 0x9E3779B1u) ^ ((bounce + 1u) * 0x85EBCA77u);
	h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
	const float u = float(h & 0xFFFFu) / 65536.0, v = float(h >> 16) / 65536.0;
	const vec3 helper = abs(normal.x) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
	const vec3 tangent = normalize(cross(helper, normal)), bitangent = cross(normal, tangent);
	const float radius = sqrt(u), angle = 6.28318531 * v;
	return tangent * (radius * cos(angle)) + bitangent * (radius * sin(angle)) + normal * sqrt(max(0.0, 1.0 - u));
}

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main()
{
	const uint ray = gl_GlobalInvocationID.x;
	const uint rays = b_Request.Config.x;
	if (ray >= rays)
		return;
	const float z = 1.0 - 2.0 * (float(ray) + 0.5) / float(rays);
	const float radius = sqrt(max(0.0, 1.0 - z * z));
	const float angle = SOUNDWAVE_GOLDEN_ANGLE * float(ray);
	vec3 direction = vec3(radius * cos(angle), z, radius * sin(angle));
	vec3 origin = b_Request.Listener.xyz;
	SoundwaveReverbRay result = SoundwaveReverbRay(0.0, 0.0, 0u, 0u);
	bool escaped = false;
	for (uint bounce = 0u; bounce < b_Request.Config.y; ++bounce)
	{
		++result.Traces;
		SoundwaveHit hit;
		if (!Soundwave_NearestHit(b_Request.Config.zw, origin, origin + direction * SOUNDWAVE_ESCAPE_DISTANCE, uvec2(0u), 0.0, hit))
		{
			escaped = true;
			break;
		}
		const SoundwaveTriangle t = b_SoundwaveTriangles.Triangles[hit.Triangle];
		const vec3 normal = Soundwave_Normal(t);
		const vec4 material = b_Request.Materials[min(floatBitsToUint(t.A.w), uint(SOUNDWAVE_MATERIALS - 1))];
		result.Absorbed += b_Request.Params.x * 0.5 * (material.x + material.y);
		result.Travelled += hit.Distance;
		++result.Hits;
		const vec3 outward = dot(direction, normal) > 0.0 ? -normal : normal;
		origin = origin + direction * hit.Distance + outward * SOUNDWAVE_SURFACE_MARGIN;
		const vec3 specular = direction - normal * (2.0 * dot(direction, normal));
		const vec3 blended = specular * (1.0 - material.z) + Diffuse(outward, ray, bounce) * material.z;
		direction = dot(blended, blended) > 0.0 ? normalize(blended) : specular;
	}
	if (escaped)
		result.Traces |= 0x80000000u;
	b_Rays.Rays[ray] = result;
}
