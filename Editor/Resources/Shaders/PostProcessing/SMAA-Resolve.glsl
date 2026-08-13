// SMAA T2x - temporal resolve.
// Ported from SMAAResolvePS (Core/vendor/smaa/SMAA.hlsl) with SMAA_REPROJECTION on.
// Compute rather than a fullscreen quad, matching the other SMAA passes.
//
// T2x renders alternating frames with two subpixel jitter positions and runs the full
// SMAA 1x pipeline on each. This pass combines the current jittered frame with the
// previous one, reprojected along the velocity buffer, giving two geometric samples
// per pixel - which is what lifts T2x above plain SMAA 1x on the shimmer that purely
// spatial AA cannot touch.
#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

layout(set = 1, binding = 0, rgba8) restrict writeonly uniform image2D o_Output;
layout(set = 1, binding = 1) uniform texture2D u_CurrentTex;  // this frame, post-SMAA
layout(set = 1, binding = 2) uniform texture2D u_PreviousTex; // last frame's resolve
layout(set = 1, binding = 3) uniform texture2D u_VelocityTex; // RG16F, current - previous UV

layout(push_constant) uniform PushConstants
{
	vec4 SMAA_RT_METRICS; // (1/w, 1/h, w, h)
	// 0 on the first frame after a cut or a resize, when there is no usable history.
	uint HasHistory;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} u_Uniforms;

layout(local_size_x = 8, local_size_y = 8) in;

// Reference default. Scales how aggressively a disagreement between the two frames
// pulls the blend back toward the current one.
#define SMAA_REPROJECTION_WEIGHT_SCALE 30.0

vec4 SamplePoint(texture2D tex, vec2 coord)
{
	return textureLod(sampler2D(tex, r_PointSampler), coord, 0.0);
}

void main()
{
	const ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Uniforms.SMAA_RT_METRICS.zw);

	if (pixelCoord.x >= size.x || pixelCoord.y >= size.y)
		return;

	const vec2 texcoord = (vec2(pixelCoord) + 0.5) * u_Uniforms.SMAA_RT_METRICS.xy;
	const vec4 current = SamplePoint(u_CurrentTex, texcoord);

	if (u_Uniforms.HasHistory == 0u)
	{
		imageStore(o_Output, pixelCoord, current);
		return;
	}

	// This engine's velocity is (current - previous) in UV space, so the previous
	// position is found by subtracting it. (The reference negates instead, because it
	// assumes a motion-blur-style buffer with the opposite sign.)
	const vec2 velocity = SamplePoint(u_VelocityTex, texcoord).rg;
	const vec2 historyUV = texcoord - velocity;

	// Off-screen history has nothing to contribute - keep the current frame rather than
	// dragging in a clamped edge texel.
	if (any(lessThan(historyUV, vec2(0.0))) || any(greaterThan(historyUV, vec2(1.0))))
	{
		imageStore(o_Output, pixelCoord, current);
		return;
	}

	const vec4 previous = SamplePoint(u_PreviousTex, historyUV);

	// Attenuate the history where the two frames disagree, which is what keeps
	// disocclusions from smearing. Weight tops out at 0.5 - an even split of the two
	// jitter positions, which is the whole point of T2x.
	const float delta = abs(current.a * current.a - previous.a * previous.a) / 5.0;
	const float weight = 0.5 * clamp(1.0 - sqrt(delta) * SMAA_REPROJECTION_WEIGHT_SCALE, 0.0, 1.0);

	imageStore(o_Output, pixelCoord, mix(current, previous, weight));
}
