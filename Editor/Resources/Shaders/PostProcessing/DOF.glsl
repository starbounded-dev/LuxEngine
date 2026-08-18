#version 450 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) out OutputBlock Output;

void main()
{
	vec4 position = vec4(a_Position.xy, 0.0, 1.0);
	Output.TexCoord = a_TexCoord;
	gl_Position = position;
}

#version 450 core
#pragma stage : frag

#include <Buffers.glslh>
#include <Samplers.glslh>

layout(location = 0) out vec4 o_Color;

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) in OutputBlock Input;

layout (set = 1, binding = 5) uniform texture2D u_Texture;
layout (set = 1, binding = 6) uniform texture2D u_DepthTexture;

layout(push_constant) uniform Uniforms
{
	vec2 DOFParams; // x = FocusDistance, y = BlurSize
} u_Uniforms;

// Bokeh depth of field, after https://blog.tuxedolabs.com/2018/05/04/bokeh-depth-of-field-in-single-pass.html
// NOTE(Yan): this is a pretty slow approach (especially on a full-size framebuffer) but it looks nice,
//            so worth experimenting with (also like most things, would be better in compute)
const float GOLDEN_ANGLE = 2.39996323;
const float MAX_BLUR_SIZE = 20.0;
const float RAD_SCALE = 1.0; // Smaller = nicer blur, larger = faster

// Depth-only taps used to find blurry foreground before committing to the full gather.
// See ComputeSearchRadius: 12 taps replace up to ~180 zero-weight iterations.
const int FOREGROUND_PROBE_RINGS = 2;
const int FOREGROUND_PROBE_TAPS = 6;

float LinearizeDepth(const float screenDepth)
{
	float depthLinearizeMul = u_Camera.DepthUnpackConsts.x;
	float depthLinearizeAdd = u_Camera.DepthUnpackConsts.y;
	return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float GetBlurSize(float depth, float focusPoint, float focusScale)
{
	float coc = clamp((1.0 / focusPoint - 1.0 / depth) * focusScale, -1.0, 1.0);
	return abs(coc) * MAX_BLUR_SIZE;
}

// How far the gather below actually has to reach for this pixel.
//
// The gather always ran to MAX_BLUR_SIZE, but a sample only contributes through
// smoothstep(radius - 0.5, radius + 0.5, sampleSize), so once no reachable surface has a
// blur radius above the current one every remaining iteration has m == 0. Those are not
// free: the accumulate degenerates to adding the running average (leaving the result
// unchanged) while still paying two texture fetches. With a typical focus setup that is
// the large majority of the loop.
float ComputeSearchRadius(vec2 texCoord, float centerDepth, float centerSize, float focusPoint, float focusScale, vec2 texelSize)
{
	// Samples BEHIND the centre are clamped to centerSize * 2 in the gather, so nothing
	// farther out can contribute. This half of the bound is exact, not an approximation.
	float searchRadius = min(centerSize * 2.0 + 1.0, MAX_BLUR_SIZE);

	// Already saturated: probing cannot raise the bound, and these are the heavily blurred
	// pixels that still pay the full gather, so the taps would be pure overhead on the
	// most expensive pixels in the frame.
	if (searchRadius >= MAX_BLUR_SIZE)
		return MAX_BLUR_SIZE;

	// Samples IN FRONT are deliberately not clamped - that is what lets a blurry
	// foreground bleed over a sharp background - so they can still reach in from the full
	// radius. Probe for them with depth-only taps rather than paying for the whole gather.
	for (int ring = 1; ring <= FOREGROUND_PROBE_RINGS; ring++)
	{
		float probeRadius = MAX_BLUR_SIZE * (float(ring) / float(FOREGROUND_PROBE_RINGS));
		for (int i = 0; i < FOREGROUND_PROBE_TAPS; i++)
		{
			float ang = GOLDEN_ANGLE * float(ring * FOREGROUND_PROBE_TAPS + i);
			vec2 tc = texCoord + vec2(cos(ang), sin(ang)) * texelSize * probeRadius;
			float probeDepth = LinearizeDepth(SampleLinear(u_DepthTexture, tc).r);
			if (probeDepth < centerDepth)
				searchRadius = max(searchRadius, GetBlurSize(probeDepth, focusPoint, focusScale));
		}
	}

	return min(searchRadius, MAX_BLUR_SIZE);
}

vec3 DepthOfField(vec2 texCoord, float focusPoint, float focusScale, vec2 texelSize)
{
	float centerDepth = LinearizeDepth(SampleLinear(u_DepthTexture, texCoord).r);
	float centerSize = GetBlurSize(centerDepth, focusPoint, focusScale);
	vec3 color = SampleLinear(u_Texture, texCoord).rgb;
	float tot = 1.0;
	float radius = RAD_SCALE;
	float searchRadius = ComputeSearchRadius(texCoord, centerDepth, centerSize, focusPoint, focusScale, texelSize);
	for (float ang = 0.0; radius < searchRadius; ang += GOLDEN_ANGLE)
	{
		vec2 tc = texCoord + vec2(cos(ang), sin(ang)) * texelSize * radius;
		vec3 sampleColor = SampleLinear(u_Texture, tc).rgb;
		float sampleDepth =  LinearizeDepth(SampleLinear(u_DepthTexture, tc).r);
		float sampleSize = GetBlurSize(sampleDepth, focusPoint, focusScale);
		if (sampleDepth > centerDepth)
			sampleSize = clamp(sampleSize, 0.0, centerSize * 2.0);
		float m = smoothstep(radius - 0.5, radius + 0.5, sampleSize);
		color += mix(color / tot, sampleColor, m);
		tot += 1.0;
		radius += RAD_SCALE / radius;
	}
	return color /= tot;
}

void main()
{
	ivec2 texSize = GetTextureSize(u_Texture, 0);
	vec2 fTexSize = vec2(float(texSize.x), float(texSize.y));

	float focusPoint = u_Uniforms.DOFParams.x;
	float blurScale = u_Uniforms.DOFParams.y;

	vec3 color = DepthOfField(Input.TexCoord, focusPoint, blurScale, 1.0 / fTexSize);
	o_Color = vec4(color, 1.0);
}