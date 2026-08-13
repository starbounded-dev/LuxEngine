// SMAA T2x - temporal resolve.
// Ported from SMAAResolvePS (Core/vendor/smaa/SMAA.hlsl) with SMAA_REPROJECTION on.
//
// T2x renders alternating frames with two subpixel jitter positions and runs the full
// SMAA 1x pipeline on each. This pass combines the current jittered frame with the
// previous one, reprojected along the velocity buffer, giving two geometric samples
// per pixel - which is what lifts T2x above plain SMAA 1x on the shimmer that purely
// spatial AA cannot touch.
#version 450 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 vs_TexCoord;

void main()
{
	vs_TexCoord = a_TexCoord;
	gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core
#pragma stage : frag

#include <Samplers.glslh>

layout(set = 1, binding = 0) uniform texture2D u_Current;  // this frame, post-SMAA
layout(set = 1, binding = 1) uniform texture2D u_Previous; // last frame's resolve
layout(set = 1, binding = 2) uniform texture2D u_Velocity; // RG16F, current - previous UV

layout(push_constant) uniform SMAAResolveSettings
{
	// 0 on the first frame after a cut or a resize, when there is no usable history.
	uint HasHistory;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} u_Resolve;

// Reference default. Scales how aggressively a disagreement between the two frames
// pulls the blend back toward the current one.
#define SMAA_REPROJECTION_WEIGHT_SCALE 30.0

layout(location = 0) in vec2 vs_TexCoord;
layout(location = 0) out vec4 o_Color;

void main()
{
	const vec2 uv = vs_TexCoord;
	const vec4 current = textureLod(sampler2D(u_Current, r_PointSampler), uv, 0.0);

	if (u_Resolve.HasHistory == 0u)
	{
		o_Color = current;
		return;
	}

	// This engine's velocity is (current - previous) in UV space, so the previous
	// position is found by subtracting it. (The reference negates instead, because it
	// assumes a motion-blur-style buffer with the opposite sign.)
	const vec2 velocity = textureLod(sampler2D(u_Velocity, r_PointSampler), uv, 0.0).rg;
	const vec2 historyUV = uv - velocity;

	// Off-screen history has nothing to contribute - keep the current frame rather than
	// dragging in a clamped edge texel.
	if (any(lessThan(historyUV, vec2(0.0))) || any(greaterThan(historyUV, vec2(1.0))))
	{
		o_Color = current;
		return;
	}

	const vec4 previous = textureLod(sampler2D(u_Previous, r_PointSampler), historyUV, 0.0);

	// Attenuate the history where the two frames disagree, which is what keeps
	// disocclusions from smearing. Weight tops out at 0.5 - an even split of the two
	// jitter positions, which is the whole point of T2x.
	const float delta = abs(current.a * current.a - previous.a * previous.a) / 5.0;
	const float weight = 0.5 * clamp(1.0 - sqrt(delta) * SMAA_REPROJECTION_WEIGHT_SCALE, 0.0, 1.0);

	o_Color = mix(current, previous, weight);
}
