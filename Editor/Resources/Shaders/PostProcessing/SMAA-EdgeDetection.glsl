// SMAA 1x - pass 1 of 3: luma edge detection.
// Based on "SMAA: Enhanced Subpixel Morphological Antialiasing" (Jimenez et al.).
//
// Runs on tone-mapped LDR colour, not HDR scene colour: morphological AA keys off
// *perceived* edges, and thresholding linear HDR misses dark edges while firing on
// bright gradients.
//
// Output is RG8 - x marks a left edge, y a top edge - consumed by the blending
// weight pass. Pixels with no edge write 0 and cost nothing downstream.
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

layout(set = 1, binding = 0) uniform texture2D u_Color;

layout(push_constant) uniform SMAASettings
{
	vec2 InvResolution; // 1 / viewport size
	// Edge sensitivity. 0.1 is the reference default; 0.05 catches more (slower),
	// 0.2 only strong edges.
	float Threshold;
	// Local contrast adaptation. Discards an edge when a neighbouring edge is this
	// many times stronger, which kills the double lines SMAA would otherwise leave
	// along high-contrast silhouettes.
	float LocalContrastAdaptationFactor;
} u_SMAA;

layout(location = 0) in vec2 vs_TexCoord;
layout(location = 0) out vec2 o_Edges;

float Luma(vec3 color)
{
	// Rec. 709 luma, matching the reference implementation.
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float SampleLuma(vec2 uv)
{
	return Luma(textureLod(sampler2D(u_Color, r_PointSampler), uv, 0.0).rgb);
}

void main()
{
	const vec2 texel = u_SMAA.InvResolution;
	const vec2 uv = vs_TexCoord;

	const float L = SampleLuma(uv);
	const float Lleft = SampleLuma(uv + vec2(-texel.x, 0.0));
	const float Ltop = SampleLuma(uv + vec2(0.0, -texel.y));

	// Delta against the left and top neighbours; an edge exists where the jump in
	// luma clears the threshold.
	vec4 delta;
	delta.xy = abs(L - vec2(Lleft, Ltop));

	vec2 edges = step(vec2(u_SMAA.Threshold), delta.xy);
	if (dot(edges, vec2(1.0)) == 0.0)
	{
		// No edge here. Writing zero rather than discarding keeps the target fully
		// defined, so the weight pass never reads undefined texels.
		o_Edges = vec2(0.0);
		return;
	}

	// Local contrast adaptation: compare this edge against the strongest edge in the
	// immediate neighbourhood (right/bottom, then the second ring left-left/top-top).
	// Without this, a strong silhouette produces a parallel second edge one pixel over
	// and the blend smears.
	const float Lright = SampleLuma(uv + vec2(texel.x, 0.0));
	const float Lbottom = SampleLuma(uv + vec2(0.0, texel.y));
	delta.zw = abs(L - vec2(Lright, Lbottom));

	vec2 maxDelta = max(delta.xy, delta.zw);

	const float Lleftleft = SampleLuma(uv + vec2(-2.0 * texel.x, 0.0));
	const float Ltoptop = SampleLuma(uv + vec2(0.0, -2.0 * texel.y));
	delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));

	maxDelta = max(maxDelta, delta.zw);
	const float finalDelta = max(maxDelta.x, maxDelta.y);

	edges *= step(finalDelta, u_SMAA.LocalContrastAdaptationFactor * delta.xy);

	o_Edges = edges;
}
