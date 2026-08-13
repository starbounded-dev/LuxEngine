// SMAA 1x - pass 3 of 3: neighbourhood blending.
// Based on "SMAA: Enhanced Subpixel Morphological Antialiasing" (Jimenez et al.).
//
// Takes the blending weights from pass 2 and resolves the final colour: each pixel is
// mixed with the neighbour its weight points at, which is what actually smooths the
// staircase. Bilinear filtering does the mixing for free - the weight is folded into
// the sample coordinate rather than applied afterwards.
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
layout(set = 1, binding = 1) uniform texture2D u_BlendWeights;

layout(push_constant) uniform SMAASettings
{
	vec2 InvResolution; // 1 / viewport size
} u_SMAA;

layout(location = 0) in vec2 vs_TexCoord;
layout(location = 0) out vec4 o_Color;

void main()
{
	const vec2 uv = vs_TexCoord;
	const vec2 texel = u_SMAA.InvResolution;

	// Gather the four weights that can affect this pixel:
	//   a = left (from this pixel), b = top (this pixel),
	//   c = right (from the right neighbour), d = bottom (from the pixel below)
	vec4 a;
	a.x = texture(sampler2D(u_BlendWeights, r_LinearSampler), uv + vec2(texel.x, 0.0)).a;  // right
	a.y = texture(sampler2D(u_BlendWeights, r_LinearSampler), uv + vec2(0.0, texel.y)).g;  // bottom
	a.wz = texture(sampler2D(u_BlendWeights, r_LinearSampler), uv).rb;                      // left, top

	// No weight anywhere means no edge touched this pixel: pass the colour through
	// untouched rather than paying for a blend that would resolve to itself.
	if (dot(a, vec4(1.0)) <= 1e-5)
	{
		o_Color = textureLod(sampler2D(u_Color, r_LinearSampler), uv, 0.0);
		return;
	}

	// Pick the dominant axis. h == true means the horizontal pair (left/right) wins,
	// so this pixel blends sideways; otherwise it blends vertically.
	const bool h = max(a.x, a.z) > max(a.y, a.w);

	// Fold the weights into two signed offsets along the winning axis, then let
	// bilinear filtering perform the actual interpolation between the two texels.
	vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
	vec2 blendingWeight = a.yw;
	if (h)
	{
		blendingOffset = vec4(a.x, 0.0, a.z, 0.0);
		blendingWeight = a.xz;
	}
	blendingWeight /= max(dot(blendingWeight, vec2(1.0)), 1e-5);

	const vec4 blendingCoord = vec4(uv, uv) + blendingOffset * vec4(texel, -texel);

	o_Color = blendingWeight.x * textureLod(sampler2D(u_Color, r_LinearSampler), blendingCoord.xy, 0.0)
	        + blendingWeight.y * textureLod(sampler2D(u_Color, r_LinearSampler), blendingCoord.zw, 0.0);
}
