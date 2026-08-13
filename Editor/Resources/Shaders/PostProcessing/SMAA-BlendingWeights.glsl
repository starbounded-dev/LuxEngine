// SMAA 1x - pass 2 of 3: blending weight calculation.
// Based on SMAA by Jorge Jimenez et al. (MIT), ported from Core/vendor/smaa/SMAA.hlsl
// and kept structurally identical to it so the two can be diffed.
//
// Compute rather than a fullscreen quad, matching the GTAO/SSR pattern. The reference's
// vertex-shader offsets are computed in main().
//
// For each edge pixel this walks along the edge to find where it ends in both
// directions, then looks up the analytic coverage area for that pattern in AreaTex.
// The walk is bilinear-accelerated: sampling *between* texels fetches two edges per
// tap, and SearchTex decodes how far the last (partial) step actually reached.
#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

layout(set = 1, binding = 0, rgba8) restrict writeonly uniform image2D o_BlendWeights;
layout(set = 1, binding = 1) uniform texture2D u_EdgeTex;
layout(set = 1, binding = 2) uniform texture2D u_AreaTex;
layout(set = 1, binding = 3) uniform texture2D u_SearchTex;

layout(push_constant) uniform PushConstants
{
	vec4 SMAA_RT_METRICS; // (1/w, 1/h, w, h)
	// Selects which AreaTex subtexture to sample, so each jittered frame gets areas
	// computed for its own subpixel offset. Zero for SMAA 1x; for T2x the reference
	// specifies (1,1,1,0) and (2,2,2,0) for the two jitter positions (@SUBSAMPLE_INDICES).
	vec4 SubsampleIndices;
} u_Uniforms;

layout(local_size_x = 8, local_size_y = 8) in;

// High preset.
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_CORNER_ROUNDING 25
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)

// Describe the layout of the vendored lookup tables - must match AreaTex/SearchTex.
#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / vec2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE vec2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE vec2(64.0, 16.0)

// Edges MUST be sampled bilinearly - the whole search scheme depends on reading
// interpolated values between texels.
vec4 SampleLinearZero(texture2D tex, vec2 coord)
{
	return textureLod(sampler2D(tex, r_LinearSampler), coord, 0.0);
}

// textureLodOffset needs a compile-time constant offset, which cannot come through a
// function parameter, so offsets are applied in UV space instead.
vec4 SampleLinearZeroOffset(texture2D tex, vec2 coord, vec2 off)
{
	return textureLod(sampler2D(tex, r_LinearSampler), coord + off * u_Uniforms.SMAA_RT_METRICS.xy, 0.0);
}

void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value)
{
	if (cond.x) variable.x = value.x;
	if (cond.y) variable.y = value.y;
}

void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value)
{
	SMAAMovc(cond.xy, variable.xy, value.xy);
	SMAAMovc(cond.zw, variable.zw, value.zw);
}

//-----------------------------------------------------------------------------
// Diagonal search
//
// Diagonals are handled first and take priority: a 45 degree staircase is one long
// diagonal, not a run of tiny horizontal steps, and treating it as the latter leaves
// it visibly ragged.

// Decodes two binary values out of one bilinear fetch. The fetch sits at a 0.25
// offset, so an active edge reads back as 0.25/1.0 (red) or 0.75/1.0 (green).
vec2 SMAADecodeDiagBilinearAccess(vec2 e)
{
	e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
	return round(e);
}

vec4 SMAADecodeDiagBilinearAccess(vec4 e)
{
	e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
	return round(e);
}

vec2 SMAASearchDiag1(vec2 texcoord, vec2 dir, out vec2 e)
{
	vec4 coord = vec4(texcoord, -1.0, 1.0);
	vec3 t = vec3(u_Uniforms.SMAA_RT_METRICS.xy, 1.0);
	e = vec2(0.0);
	while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9)
	{
		coord.xyz = fma(t, vec3(dir, 1.0), coord.xyz);
		e = SampleLinearZero(u_EdgeTex, coord.xy).rg;
		coord.w = dot(e, vec2(0.5, 0.5));
	}
	return coord.zw;
}

vec2 SMAASearchDiag2(vec2 texcoord, vec2 dir, out vec2 e)
{
	vec4 coord = vec4(texcoord, -1.0, 1.0);
	coord.x += 0.25 * u_Uniforms.SMAA_RT_METRICS.x; // @SearchDiag2Optimization
	vec3 t = vec3(u_Uniforms.SMAA_RT_METRICS.xy, 1.0);
	e = vec2(0.0);
	while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9)
	{
		coord.xyz = fma(t, vec3(dir, 1.0), coord.xyz);

		// Fetch both edges at once using bilinear filtering, then unpack.
		e = SampleLinearZero(u_EdgeTex, coord.xy).rg;
		e = SMAADecodeDiagBilinearAccess(e);

		coord.w = dot(e, vec2(0.5, 0.5));
	}
	return coord.zw;
}

// As SMAAArea, but for diagonal patterns. Diagonal areas live in the second half of
// AreaTex, hence the 0.5 shift in x.
vec2 SMAAAreaDiag(vec2 dist, vec2 e, float offset)
{
	vec2 texcoord = fma(vec2(SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);
	texcoord = fma(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);
	texcoord.x += 0.5;
	texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;
	return SampleLinearZero(u_AreaTex, texcoord).rg;
}

vec2 SMAACalculateDiagWeights(vec2 texcoord, vec2 e, vec4 subsampleIndices)
{
	vec2 weights = vec2(0.0);

	vec4 d;
	vec2 end;
	if (e.r > 0.0)
	{
		d.xz = SMAASearchDiag1(texcoord, vec2(-1.0, 1.0), end);
		d.x += float(end.y > 0.9);
	}
	else
		d.xz = vec2(0.0);
	d.yw = SMAASearchDiag1(texcoord, vec2(1.0, -1.0), end);

	if (d.x + d.y > 2.0) // d.x + d.y + 1 > 3
	{
		vec4 coords = fma(vec4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), u_Uniforms.SMAA_RT_METRICS.xyxy, texcoord.xyxy);
		vec4 c;
		c.xy = SampleLinearZeroOffset(u_EdgeTex, coords.xy, vec2(-1.0, 0.0)).rg;
		c.zw = SampleLinearZeroOffset(u_EdgeTex, coords.zw, vec2(1.0, 0.0)).rg;
		c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

		// Merge the crossing edges on each side into one value, then discard the side
		// where the end of the line was never found.
		vec2 cc = fma(vec2(2.0), c.xz, c.yw);
		SMAAMovc(bvec2(step(0.9, d.zw)), cc, vec2(0.0));

		weights += SMAAAreaDiag(d.xy, cc, subsampleIndices.z);
	}

	d.xz = SMAASearchDiag2(texcoord, vec2(-1.0, -1.0), end);
	if (SampleLinearZeroOffset(u_EdgeTex, texcoord, vec2(1.0, 0.0)).r > 0.0)
	{
		d.yw = SMAASearchDiag2(texcoord, vec2(1.0, 1.0), end);
		d.y += float(end.y > 0.9);
	}
	else
		d.yw = vec2(0.0);

	if (d.x + d.y > 2.0)
	{
		vec4 coords = fma(vec4(-d.x, -d.x, d.y, d.y), u_Uniforms.SMAA_RT_METRICS.xyxy, texcoord.xyxy);
		vec4 c;
		c.x  = SampleLinearZeroOffset(u_EdgeTex, coords.xy, vec2(-1.0, 0.0)).g;
		c.y  = SampleLinearZeroOffset(u_EdgeTex, coords.xy, vec2(0.0, -1.0)).r;
		c.zw = SampleLinearZeroOffset(u_EdgeTex, coords.zw, vec2(1.0, 0.0)).gr;
		vec2 cc = fma(vec2(2.0), c.xz, c.yw);
		SMAAMovc(bvec2(step(0.9, d.zw)), cc, vec2(0.0));

		// This direction's areas come back transposed.
		weights += SMAAAreaDiag(d.xy, cc, subsampleIndices.w).gr;
	}

	return weights;
}

//-----------------------------------------------------------------------------
// Horizontal/vertical search

// How much length to add in the final step of a search. Takes the bilinearly
// interpolated edge and returns 0, 1 or 2 depending on which edges are active.
float SMAASearchLength(vec2 e, float offset)
{
	// The texture is flipped vertically, with the left and right cases each taking half
	// of the space horizontally.
	vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
	vec2 bias  = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);
	scale += vec2(-1.0, 1.0);
	bias  += vec2(0.5, -0.5);

	// Uses the *packed* size because the texture is cropped.
	scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
	bias  *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

	// Point-sampled: this is a lookup table, and interpolating between entries returns
	// distances that do not exist.
	return textureLod(sampler2D(u_SearchTex, r_PointSampler), fma(scale, e, bias), 0.0).r;
}

float SMAASearchXLeft(vec2 texcoord, float end)
{
	vec2 e = vec2(0.0, 1.0);
	while (texcoord.x > end
		&& e.g > 0.8281   // is there some edge not activated?
		&& e.r == 0.0)    // or a crossing edge that breaks the line?
	{
		e = SampleLinearZero(u_EdgeTex, texcoord).rg;
		texcoord = fma(-vec2(2.0, 0.0), u_Uniforms.SMAA_RT_METRICS.xy, texcoord);
	}
	float offset = fma(-(255.0 / 127.0), SMAASearchLength(e, 0.0), 3.25);
	return fma(u_Uniforms.SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchXRight(vec2 texcoord, float end)
{
	vec2 e = vec2(0.0, 1.0);
	while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0)
	{
		e = SampleLinearZero(u_EdgeTex, texcoord).rg;
		texcoord = fma(vec2(2.0, 0.0), u_Uniforms.SMAA_RT_METRICS.xy, texcoord);
	}
	float offset = fma(-(255.0 / 127.0), SMAASearchLength(e, 0.5), 3.25);
	return fma(-u_Uniforms.SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchYUp(vec2 texcoord, float end)
{
	vec2 e = vec2(1.0, 0.0);
	while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0)
	{
		e = SampleLinearZero(u_EdgeTex, texcoord).rg;
		texcoord = fma(-vec2(0.0, 2.0), u_Uniforms.SMAA_RT_METRICS.xy, texcoord);
	}
	float offset = fma(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.0), 3.25);
	return fma(u_Uniforms.SMAA_RT_METRICS.y, offset, texcoord.y);
}

float SMAASearchYDown(vec2 texcoord, float end)
{
	vec2 e = vec2(1.0, 0.0);
	while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0)
	{
		e = SampleLinearZero(u_EdgeTex, texcoord).rg;
		texcoord = fma(vec2(0.0, 2.0), u_Uniforms.SMAA_RT_METRICS.xy, texcoord);
	}
	float offset = fma(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.5), 3.25);
	return fma(-u_Uniforms.SMAA_RT_METRICS.y, offset, texcoord.y);
}

//-----------------------------------------------------------------------------
// Area lookup

// AreaTex is quadratically compressed, hence the sqrt applied by the caller.
vec2 SMAAArea(vec2 dist, float e1, float e2, float offset)
{
	// Rounding prevents precision errors in the bilinear fetch.
	vec2 texcoord = fma(vec2(SMAA_AREATEX_MAX_DISTANCE), round(4.0 * vec2(e1, e2)), dist);
	texcoord = fma(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);
	texcoord.y = fma(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);
	return SampleLinearZero(u_AreaTex, texcoord).rg;
}

//-----------------------------------------------------------------------------
// Corner detection - reduces blending near corners so they are not rounded off.

void SMAADetectHorizontalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d)
{
	vec2 leftRight = step(d.xy, d.yx);
	vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
	rounding /= leftRight.x + leftRight.y; // reduce blending mid-line

	vec2 factor = vec2(1.0);
	factor.x -= rounding.x * SampleLinearZeroOffset(u_EdgeTex, texcoord.xy, vec2(0.0, 1.0)).r;
	factor.x -= rounding.y * SampleLinearZeroOffset(u_EdgeTex, texcoord.zw, vec2(1.0, 1.0)).r;
	factor.y -= rounding.x * SampleLinearZeroOffset(u_EdgeTex, texcoord.xy, vec2(0.0, -2.0)).r;
	factor.y -= rounding.y * SampleLinearZeroOffset(u_EdgeTex, texcoord.zw, vec2(1.0, -2.0)).r;

	weights *= clamp(factor, 0.0, 1.0);
}

void SMAADetectVerticalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d)
{
	vec2 leftRight = step(d.xy, d.yx);
	vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
	rounding /= leftRight.x + leftRight.y;

	vec2 factor = vec2(1.0);
	factor.x -= rounding.x * SampleLinearZeroOffset(u_EdgeTex, texcoord.xy, vec2(1.0, 0.0)).g;
	factor.x -= rounding.y * SampleLinearZeroOffset(u_EdgeTex, texcoord.zw, vec2(1.0, 1.0)).g;
	factor.y -= rounding.x * SampleLinearZeroOffset(u_EdgeTex, texcoord.xy, vec2(-2.0, 0.0)).g;
	factor.y -= rounding.y * SampleLinearZeroOffset(u_EdgeTex, texcoord.zw, vec2(-2.0, 1.0)).g;

	weights *= clamp(factor, 0.0, 1.0);
}

//-----------------------------------------------------------------------------

void main()
{
	const ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 size = ivec2(u_Uniforms.SMAA_RT_METRICS.zw);

	if (pixelCoord.x >= size.x || pixelCoord.y >= size.y)
		return;

	const vec2 texcoord = (vec2(pixelCoord) + 0.5) * u_Uniforms.SMAA_RT_METRICS.xy;
	const vec2 pixcoord = texcoord * u_Uniforms.SMAA_RT_METRICS.zw;

	// The reference's vertex-shader offsets. Sampling at -0.25 / -0.125 lands between
	// texels so one bilinear fetch returns several edges (@PSEUDO_GATHER4).
	const vec4 offset0 = fma(u_Uniforms.SMAA_RT_METRICS.xyxy, vec4(-0.25, -0.125, 1.25, -0.125), texcoord.xyxy);
	const vec4 offset1 = fma(u_Uniforms.SMAA_RT_METRICS.xyxy, vec4(-0.125, -0.25, -0.125, 1.25), texcoord.xyxy);
	const vec4 offset2 = fma(u_Uniforms.SMAA_RT_METRICS.xxyy,
		vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),
		vec4(offset0.xz, offset1.yw));

	vec4 weights = vec4(0.0);
	const vec4 subsampleIndices = u_Uniforms.SubsampleIndices;

	vec2 e = SampleLinearZero(u_EdgeTex, texcoord).rg;

	if (e.g > 0.0) // edge at north
	{
		// Diagonals have both a north and a west edge, so searching one boundary finds
		// them. They take priority over the horizontal/vertical treatment below.
		weights.rg = SMAACalculateDiagWeights(texcoord, e, subsampleIndices);

		if (weights.r == -weights.g) // i.e. weights.r + weights.g == 0 - no diagonal
		{
			vec2 d;
			vec3 coords;

			coords.x = SMAASearchXLeft(offset0.xy, offset2.x);
			coords.y = offset1.y; // @CROSSING_OFFSET
			d.x = coords.x;

			// Sampling at -0.25 lets one tap disambiguate what value each edge has.
			float e1 = SampleLinearZero(u_EdgeTex, coords.xy).r;

			coords.z = SMAASearchXRight(offset0.zw, offset2.y);
			d.y = coords.z;

			// Distances in pixel units.
			d = abs(round(fma(u_Uniforms.SMAA_RT_METRICS.zz, d, -pixcoord.xx)));
			vec2 sqrt_d = sqrt(d);

			float e2 = SampleLinearZeroOffset(u_EdgeTex, coords.zy, vec2(1.0, 0.0)).r;

			weights.rg = SMAAArea(sqrt_d, e1, e2, subsampleIndices.y);

			coords.y = texcoord.y;
			SMAADetectHorizontalCornerPattern(weights.rg, coords.xyzy, d);
		}
		else
			e.r = 0.0; // a diagonal was found - skip vertical processing
	}

	if (e.r > 0.0) // edge at west
	{
		vec2 d;
		vec3 coords;

		coords.y = SMAASearchYUp(offset1.xy, offset2.z);
		coords.x = offset0.x;
		d.x = coords.y;

		float e1 = SampleLinearZero(u_EdgeTex, coords.xy).g;

		coords.z = SMAASearchYDown(offset1.zw, offset2.w);
		d.y = coords.z;

		d = abs(round(fma(u_Uniforms.SMAA_RT_METRICS.ww, d, -pixcoord.yy)));
		vec2 sqrt_d = sqrt(d);

		float e2 = SampleLinearZeroOffset(u_EdgeTex, coords.xz, vec2(0.0, 1.0)).g;

		weights.ba = SMAAArea(sqrt_d, e1, e2, subsampleIndices.x);

		coords.x = texcoord.x;
		SMAADetectVerticalCornerPattern(weights.ba, coords.xyxz, d);
	}

	imageStore(o_BlendWeights, pixelCoord, weights);
}
