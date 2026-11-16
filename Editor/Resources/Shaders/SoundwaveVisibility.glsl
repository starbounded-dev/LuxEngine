// Independent Soundwave propagation visibility rays. A worker prepares bounded paths and
// segment endpoints; this stage tests their legs in parallel against the shared flattened
// acoustic geometry. Binding slots 35-38 are unique across the engine shader library.
#version 450 core
#pragma stage : comp

#include <Soundwave.glslh>

layout(std430, set = 1, binding = 35) readonly buffer SoundwaveVisibilityRequest
{
	uvec4 Config; // root 0, root 1, segment count, portal count
} b_VisibilityRequest;

struct SoundwaveVisibilitySegment
{
	vec4 From;
	vec4 To;
	uvec4 Metadata; // ignored instance low/high, floatBits(margin), reserved
};
layout(std430, set = 1, binding = 36) readonly buffer SoundwaveVisibilitySegments
{
	SoundwaveVisibilitySegment Segments[];
} b_VisibilitySegments;

struct SoundwavePortal
{
	mat4 WorldToBox;
	uvec4 Instance; // low, high, active, reserved
};
layout(std430, set = 1, binding = 37) readonly buffer SoundwaveVisibilityPortals
{
	SoundwavePortal Portals[];
} b_VisibilityPortals;

layout(std430, set = 1, binding = 38) writeonly buffer SoundwaveVisibilityResults
{
	uint Results[]; // 0 clear, 1 blocked, 2 invalid
} b_VisibilityResults;

bool Finite(vec3 value)
{
	return !any(isnan(value)) && !any(isinf(value));
}

// Mirrors Solve.cpp's analytic clipping of a portal in unit-box coordinates.
bool ClipPortal(vec3 from, vec3 to, float lengthWorld, float margin)
{
	const vec3 direction = to - from;
	float start = 0.0, end = 1.0;
	for (int axis = 0; axis < 3; ++axis)
	{
		const float delta = direction[axis];
		if (abs(delta) < 1e-8)
		{
			if (from[axis] < -1.0 || from[axis] > 1.0)
				return false;
			continue;
		}
		const float a = (-1.0 - from[axis]) / delta;
		const float b = (1.0 - from[axis]) / delta;
		start = max(start, min(a, b));
		end = min(end, max(a, b));
		if (start >= end)
			return false;
	}
	return end * lengthWorld > margin && start * lengthWorld < lengthWorld - margin;
}

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main()
{
	const uint index = gl_GlobalInvocationID.x;
	if (index >= b_VisibilityRequest.Config.z)
		return;
	const SoundwaveVisibilitySegment segment = b_VisibilitySegments.Segments[index];
	const vec3 from = segment.From.xyz, to = segment.To.xyz;
	const float margin = uintBitsToFloat(segment.Metadata.z);
	const float lengthWorld = length(to - from);
	if (!Finite(from) || !Finite(to) || isnan(lengthWorld) || isinf(lengthWorld) ||
		isnan(margin) || isinf(margin) || margin < 0.0)
	{
		b_VisibilityResults.Results[index] = 2u;
		return;
	}
	SoundwaveHit hit;
	if (Soundwave_NearestHit(b_VisibilityRequest.Config.xy, from, to,
		segment.Metadata.xy, margin, hit))
	{
		b_VisibilityResults.Results[index] = 1u;
		return;
	}
	for (uint i = 0u; i < b_VisibilityRequest.Config.w; ++i)
	{
		const SoundwavePortal portal = b_VisibilityPortals.Portals[i];
		if (portal.Instance.z == 0u)
			continue;
		const vec3 a = (portal.WorldToBox * vec4(from, 1.0)).xyz;
		const vec3 b = (portal.WorldToBox * vec4(to, 1.0)).xyz;
		if (!Finite(a) || !Finite(b) || !Finite(b - a))
		{
			b_VisibilityResults.Results[index] = 2u;
			return;
		}
		if (ClipPortal(a, b, lengthWorld, margin))
		{
			b_VisibilityResults.Results[index] = 1u;
			return;
		}
	}
	b_VisibilityResults.Results[index] = 0u;
}
