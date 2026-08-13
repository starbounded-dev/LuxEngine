// SMAA 1x - pass 1 of 3: luma edge detection.
// Based on SMAA by Jorge Jimenez et al. (MIT), ported from Core/vendor/smaa/SMAA.hlsl.
//
// Compute rather than a fullscreen quad, matching the GTAO/SSR pattern: storage images
// instead of framebuffers, no vertex work, and the vertex-shader offsets folded into
// main().
//
// Runs on tone-mapped LDR colour, not HDR scene colour: morphological AA keys off
// *perceived* edges, and thresholding linear HDR misses dark edges while firing on
// bright gradients.
#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

layout(set = 1, binding = 0, rg8) restrict writeonly uniform image2D o_Edges;
layout(set = 1, binding = 1) uniform texture2D u_InputTex;

layout(push_constant) uniform PushConstants
{
	vec4 SMAA_RT_METRICS; // (1/w, 1/h, w, h)
	// Edge sensitivity. 0.1 is the reference default; lower catches more (slower).
	float Threshold;
	// Discards an edge when a neighbouring one is this many times stronger, which kills
	// the doubled lines SMAA would otherwise leave along high-contrast silhouettes.
	float LocalContrastAdaptationFactor;
	float _pad0;
	float _pad1;
} u_Uniforms;

layout(local_size_x = 8, local_size_y = 8) in;

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

	const vec4 offset0 = texcoord.xyxy + u_Uniforms.SMAA_RT_METRICS.xyxy * vec4(-1.0, 0.0, 0.0, -1.0);
	const vec4 offset1 = texcoord.xyxy + u_Uniforms.SMAA_RT_METRICS.xyxy * vec4( 1.0, 0.0, 0.0,  1.0);
	const vec4 offset2 = texcoord.xyxy + u_Uniforms.SMAA_RT_METRICS.xyxy * vec4(-2.0, 0.0, 0.0, -2.0);

	const vec3 weights = vec3(0.2126, 0.7152, 0.0722); // Rec. 709 luma
	const float L     = dot(SamplePoint(u_InputTex, texcoord).rgb, weights);
	const float Lleft = dot(SamplePoint(u_InputTex, offset0.xy).rgb, weights);
	const float Ltop  = dot(SamplePoint(u_InputTex, offset0.zw).rgb, weights);

	vec4 delta;
	delta.xy = abs(L - vec2(Lleft, Ltop));
	vec2 edges = step(vec2(u_Uniforms.Threshold), delta.xy);

	// No edge here. Writing zero rather than leaving the texel untouched keeps the
	// target fully defined, so the weight pass never reads stale data.
	if (dot(edges, vec2(1.0)) == 0.0)
	{
		imageStore(o_Edges, pixelCoord, vec4(0.0));
		return;
	}

	const float Lright  = dot(SamplePoint(u_InputTex, offset1.xy).rgb, weights);
	const float Lbottom = dot(SamplePoint(u_InputTex, offset1.zw).rgb, weights);
	delta.zw = abs(L - vec2(Lright, Lbottom));

	vec2 maxDelta = max(delta.xy, delta.zw);

	const float Lleftleft = dot(SamplePoint(u_InputTex, offset2.xy).rgb, weights);
	const float Ltoptop   = dot(SamplePoint(u_InputTex, offset2.zw).rgb, weights);
	delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));

	maxDelta = max(maxDelta, delta.zw);
	const float finalDelta = max(maxDelta.x, maxDelta.y);

	edges *= step(finalDelta, u_Uniforms.LocalContrastAdaptationFactor * delta.xy);

	imageStore(o_Edges, pixelCoord, vec4(edges, 0.0, 0.0));
}
