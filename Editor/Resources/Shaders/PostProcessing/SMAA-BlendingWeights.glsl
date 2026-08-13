// SMAA 1x - pass 2 of 3: blending weight calculation.
// Ported from the reference implementation (Core/vendor/smaa/SMAA.hlsl, MIT,
// Jimenez et al.). Kept structurally identical to the original so it can be diffed
// against it; HLSL mad(a,b,c) is written out as a * b + c.
//
// For each edge pixel this walks along the edge to find where it ends in both
// directions, then looks up the analytic coverage area for that pattern in AreaTex.
// The walk is bilinear-accelerated: sampling *between* texels fetches two edges per
// tap, and SearchTex decodes how far the last (partial) step actually reached.
//
// Diagonal detection is not ported (SMAA_DISABLE_DIAG_DETECTION in the reference).
// Corner detection is.
#version 450 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(push_constant) uniform SMAASettings
{
	vec4 RTMetrics; // (1/width, 1/height, width, height)
} u_SMAA;

// SMAA_MAX_SEARCH_STEPS 16 == the reference's High/Ultra presets. This bounds how far
// along an edge the search can walk, so it caps the length of a staircase SMAA can
// straighten - not the cost of a single tap.
#define SMAA_MAX_SEARCH_STEPS 16

layout(location = 0) out vec2 vs_TexCoord;
layout(location = 1) out vec2 vs_PixCoord;
layout(location = 2) out vec4 vs_Offset0;
layout(location = 3) out vec4 vs_Offset1;
layout(location = 4) out vec4 vs_Offset2;

void main()
{
	vs_TexCoord = a_TexCoord;
	vs_PixCoord = a_TexCoord * u_SMAA.RTMetrics.zw;

	// Offsets used by the searches (@PSEUDO_GATHER4 in the reference): sampling at
	// -0.25 / -0.125 lands between texels so one bilinear fetch returns several edges.
	vs_Offset0 = u_SMAA.RTMetrics.xyxy * vec4(-0.25, -0.125, 1.25, -0.125) + a_TexCoord.xyxy;
	vs_Offset1 = u_SMAA.RTMetrics.xyxy * vec4(-0.125, -0.25, -0.125, 1.25) + a_TexCoord.xyxy;

	// Ends of the search loops.
	vs_Offset2 = u_SMAA.RTMetrics.xxyy * (vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS))
	           + vec4(vs_Offset0.xz, vs_Offset1.yw);

	gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core
#pragma stage : frag

#include <Samplers.glslh>

layout(set = 1, binding = 0) uniform texture2D u_Edges;    // RG8 from pass 1
layout(set = 1, binding = 1) uniform texture2D u_AreaTex;  // RG8 lookup, 160x560
layout(set = 1, binding = 2) uniform texture2D u_SearchTex; // R8 lookup, 64x16

layout(push_constant) uniform SMAASettings
{
	vec4 RTMetrics; // (1/width, 1/height, width, height)
} u_SMAA;

#define SMAA_MAX_SEARCH_STEPS 16

// Reference constants (SMAA.hlsl lines 517-523). These describe how the lookup
// tables are laid out and must match the vendored AreaTex/SearchTex exactly.
#define SMAA_AREATEX_MAX_DISTANCE 16.0
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / vec2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE vec2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE vec2(64.0, 16.0)
#define SMAA_CORNER_ROUNDING 25
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)

layout(location = 0) in vec2 vs_TexCoord;
layout(location = 1) in vec2 vs_PixCoord;
layout(location = 2) in vec4 vs_Offset0;
layout(location = 3) in vec4 vs_Offset1;
layout(location = 4) in vec4 vs_Offset2;

layout(location = 0) out vec4 o_Weights;

// The edge texture MUST be sampled bilinearly - the whole search scheme depends on
// getting interpolated values between texels.
vec2 SampleEdges(vec2 uv)
{
	return textureLod(sampler2D(u_Edges, r_LinearSampler), uv, 0.0).rg;
}

// How much length to add in the final step of a search. Takes the bilinearly
// interpolated edge and returns 0, 1 or 2 depending on which edges are active.
float SMAASearchLength(vec2 e, float offset)
{
	// The texture is flipped vertically, with the left and right cases each taking
	// half of the space horizontally.
	vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
	vec2 bias = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);

	// Scale and bias to access texel centres.
	scale += vec2(-1.0, 1.0);
	bias += vec2(0.5, -0.5);

	// To texcoords. Uses the *packed* size because the texture is cropped.
	scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
	bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

	// Point-sampled: this is a lookup table, and interpolating between entries
	// returns distances that do not exist.
	return textureLod(sampler2D(u_SearchTex, r_PointSampler), scale * e + bias, 0.0).r;
}

float SMAASearchXLeft(vec2 texcoord, float end)
{
	vec2 e = vec2(0.0, 1.0);
	while (texcoord.x > end
		&& e.g > 0.8281   // is there some edge not activated?
		&& e.r == 0.0)    // or a crossing edge that breaks the line?
	{
		e = SampleEdges(texcoord);
		texcoord = -vec2(2.0, 0.0) * u_SMAA.RTMetrics.xy + texcoord;
	}

	float offset = -(255.0 / 127.0) * SMAASearchLength(e, 0.0) + 3.25;
	return u_SMAA.RTMetrics.x * offset + texcoord.x;
}

float SMAASearchXRight(vec2 texcoord, float end)
{
	vec2 e = vec2(0.0, 1.0);
	while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0)
	{
		e = SampleEdges(texcoord);
		texcoord = vec2(2.0, 0.0) * u_SMAA.RTMetrics.xy + texcoord;
	}

	float offset = -(255.0 / 127.0) * SMAASearchLength(e, 0.5) + 3.25;
	return -u_SMAA.RTMetrics.x * offset + texcoord.x;
}

float SMAASearchYUp(vec2 texcoord, float end)
{
	vec2 e = vec2(1.0, 0.0);
	while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0)
	{
		e = SampleEdges(texcoord);
		texcoord = -vec2(0.0, 2.0) * u_SMAA.RTMetrics.xy + texcoord;
	}

	float offset = -(255.0 / 127.0) * SMAASearchLength(e.gr, 0.0) + 3.25;
	return u_SMAA.RTMetrics.y * offset + texcoord.y;
}

float SMAASearchYDown(vec2 texcoord, float end)
{
	vec2 e = vec2(1.0, 0.0);
	while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0)
	{
		e = SampleEdges(texcoord);
		texcoord = vec2(0.0, 2.0) * u_SMAA.RTMetrics.xy + texcoord;
	}

	float offset = -(255.0 / 127.0) * SMAASearchLength(e.gr, 0.5) + 3.25;
	return -u_SMAA.RTMetrics.y * offset + texcoord.y;
}

// Given the distance to both ends of the edge and the two crossing edges, look up
// the coverage area on each side. AreaTex is quadratically compressed, hence the
// sqrt applied by the caller.
vec2 SMAAArea(vec2 dist, float e1, float e2, float offset)
{
	// Rounding prevents precision errors in the bilinear fetch.
	vec2 texcoord = vec2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE) * round(4.0 * vec2(e1, e2)) + dist;

	// Scale and bias into texel space.
	texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + (0.5 * SMAA_AREATEX_PIXEL_SIZE);

	// Move to the right subtexture for the subpixel offset (always 0 for SMAA 1x).
	texcoord.y = SMAA_AREATEX_SUBTEX_SIZE * offset + texcoord.y;

	return textureLod(sampler2D(u_AreaTex, r_LinearSampler), texcoord, 0.0).rg;
}

// Corner detection: reduces blending near corners so they do not get rounded off.
void SMAADetectHorizontalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d)
{
	vec2 leftRight = step(d.xy, d.yx);
	vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

	rounding /= leftRight.x + leftRight.y; // reduce blending mid-line

	vec2 factor = vec2(1.0);
	factor.x -= rounding.x * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.xy, 0.0, ivec2(0, 1)).r;
	factor.x -= rounding.y * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.zw, 0.0, ivec2(1, 1)).r;
	factor.y -= rounding.x * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.xy, 0.0, ivec2(0, -2)).r;
	factor.y -= rounding.y * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.zw, 0.0, ivec2(1, -2)).r;

	weights *= clamp(factor, 0.0, 1.0);
}

void SMAADetectVerticalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d)
{
	vec2 leftRight = step(d.xy, d.yx);
	vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

	rounding /= leftRight.x + leftRight.y;

	vec2 factor = vec2(1.0);
	factor.x -= rounding.x * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.xy, 0.0, ivec2(1, 0)).g;
	factor.x -= rounding.y * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.zw, 0.0, ivec2(1, 1)).g;
	factor.y -= rounding.x * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.xy, 0.0, ivec2(-2, 0)).g;
	factor.y -= rounding.y * textureLodOffset(sampler2D(u_Edges, r_LinearSampler), texcoord.zw, 0.0, ivec2(-2, 1)).g;

	weights *= clamp(factor, 0.0, 1.0);
}

void main()
{
	vec4 weights = vec4(0.0);
	vec2 e = SampleEdges(vs_TexCoord);

	if (e.g > 0.0) // edge at north
	{
		vec2 d;
		vec3 coords;

		// Distance to the left.
		coords.x = SMAASearchXLeft(vs_Offset0.xy, vs_Offset2.x);
		coords.y = vs_Offset1.y; // @CROSSING_OFFSET
		d.x = coords.x;

		// Fetch the left crossing edges - sampling at -0.25 lets one tap
		// disambiguate what value each edge has.
		float e1 = textureLod(sampler2D(u_Edges, r_LinearSampler), coords.xy, 0.0).r;

		// Distance to the right.
		coords.z = SMAASearchXRight(vs_Offset0.zw, vs_Offset2.y);
		d.y = coords.z;

		// Convert to pixel units.
		d = abs(round(u_SMAA.RTMetrics.zz * d - vs_PixCoord.xx));

		// AreaTex is quadratically compressed.
		vec2 sqrt_d = sqrt(d);

		float e2 = textureLodOffset(sampler2D(u_Edges, r_LinearSampler), vec2(coords.z, coords.y), 0.0, ivec2(1, 0)).r;

		weights.rg = SMAAArea(sqrt_d, e1, e2, 0.0);

		coords.y = vs_TexCoord.y;
		SMAADetectHorizontalCornerPattern(weights.rg, vec4(coords.x, coords.y, coords.z, coords.y), d);
	}

	if (e.r > 0.0) // edge at west
	{
		vec2 d;
		vec3 coords;

		// Distance to the top.
		coords.y = SMAASearchYUp(vs_Offset1.xy, vs_Offset2.z);
		coords.x = vs_Offset0.x;
		d.x = coords.y;

		float e1 = textureLod(sampler2D(u_Edges, r_LinearSampler), coords.xy, 0.0).g;

		// Distance to the bottom.
		coords.z = SMAASearchYDown(vs_Offset1.zw, vs_Offset2.w);
		d.y = coords.z;

		d = abs(round(u_SMAA.RTMetrics.ww * d - vs_PixCoord.yy));

		vec2 sqrt_d = sqrt(d);

		float e2 = textureLodOffset(sampler2D(u_Edges, r_LinearSampler), vec2(coords.x, coords.z), 0.0, ivec2(0, 1)).g;

		weights.ba = SMAAArea(sqrt_d, e1, e2, 0.0);

		coords.x = vs_TexCoord.x;
		SMAADetectVerticalCornerPattern(weights.ba, vec4(coords.x, coords.y, coords.x, coords.z), d);
	}

	o_Weights = weights;
}
