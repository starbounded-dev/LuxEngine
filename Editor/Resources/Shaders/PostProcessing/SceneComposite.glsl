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

#define HZ_RENDERER_EDGE_OUTLINE_EFFECT 0
#define HZ_RENDERER_FOG_EFFECT 0

#include <Buffers.glslh>
#include <Samplers.glslh>

layout(location = 0) out vec4 o_Color;

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) in OutputBlock Input;

layout (set = 1, binding = 0) uniform texture2D u_Texture;
layout (set = 1, binding = 1) uniform texture2D u_BloomTexture;
layout (set = 1, binding = 2) uniform texture2D u_BloomDirtTexture;
layout (set = 1, binding = 3) uniform texture2D u_DepthTexture;
layout (set = 1, binding = 4) uniform texture2D u_TransparentDepthTexture;

#if HZ_RENDERER_EDGE_OUTLINE_EFFECT
layout (set = 1, binding = 5) uniform texture2D u_EdgeTexture;
#endif

// Histogram auto-exposure result (written by LuminanceAverage.glsl). Sampled only
// when UseAutoExposure is set; otherwise the manual/camera exposure push constant is used.
layout(std430, set = 1, binding = 6) readonly buffer ExposureStateBuffer
{
	float AdaptedLuminance;
	float Exposure;
} b_Exposure;

layout(push_constant) uniform Uniforms
{
	float Exposure;
	float BloomIntensity;
	float BloomDirtIntensity;
	float Opacity;
	float Time;
	vec4 ColorFilterSaturation;
	vec2 ContrastGamma;
	int UseAutoExposure;
	int TonemapOperator; // 0 = ACES, 1 = AgX, 2 = None
	vec4 Lift;           // rgb = lift (shadows)
	vec4 GradeGamma;     // rgb = grading gamma (midtones)
	vec4 Gain;           // rgb = gain (highlights)
} u_Uniforms;

#define TONEMAP_ACES 0
#define TONEMAP_AGX  1
#define TONEMAP_NONE 2

float LinearizeDepth(const float screenDepth)
{
	float depthLinearizeMul = u_Camera.DepthUnpackConsts.x;
	float depthLinearizeAdd = u_Camera.DepthUnpackConsts.y;
	return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

vec4 LinearizeDepth(vec4 deviceZs)
{
    return vec4(LinearizeDepth(deviceZs.x), LinearizeDepth(deviceZs.y), LinearizeDepth(deviceZs.z), LinearizeDepth(deviceZs.w));
}

vec3 UpsampleTent9(texture2D tex, float lod, vec2 uv, vec2 texelSize, float radius)
{
	vec4 offset = texelSize.xyxy * vec4(1.0f, 1.0f, -1.0f, 0.0f) * radius;

	// Center
	vec3 result = SampleLinearLOD(tex, uv, lod).rgb * 4.0f;

	result += SampleLinearLOD(tex, uv - offset.xy, lod).rgb;
	result += SampleLinearLOD(tex, uv - offset.wy, lod).rgb * 2.0;
	result += SampleLinearLOD(tex, uv - offset.zy, lod).rgb;

	result += SampleLinearLOD(tex, uv + offset.zw, lod).rgb * 2.0;
	result += SampleLinearLOD(tex, uv + offset.xw, lod).rgb * 2.0;

	result += SampleLinearLOD(tex, uv + offset.zy, lod).rgb;
	result += SampleLinearLOD(tex, uv + offset.wy, lod).rgb * 2.0;
	result += SampleLinearLOD(tex, uv + offset.xy, lod).rgb;

	return result * (1.0f / 16.0f);
}

// Based on http://www.oscars.org/science-technology/sci-tech-projects/aces
vec3 ACESTonemap(vec3 color)
{
	mat3 m1 = mat3(
		0.59719, 0.07600, 0.02840,
		0.35458, 0.90834, 0.13383,
		0.04823, 0.01566, 0.83777
	);
	mat3 m2 = mat3(
		1.60475, -0.10208, -0.00327,
		-0.53108, 1.10813, -0.07276,
		-0.07367, -0.00605, 1.07602
	);
	vec3 v = m1 * color;
	vec3 a = v * (v + 0.0245786) - 0.000090537;
	vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	return clamp(m2 * (a / b), 0.0, 1.0);
}

// AgX tonemapper (minimal approximation). Returns a ~linear result so the shared
// display-gamma encoding can be applied afterwards, matching the ACES path.
// Reference: Troy Sobotka's AgX; minimal fit popularised by Benjamin Wrensch.
const mat3 AgXInsetMatrix = mat3(
	0.8566271533159830, 0.0951212405381588, 0.0482516061458583,
	0.1373189729298470, 0.7612419906025910, 0.1014390364675620,
	0.1118982129999500, 0.0767994186031903, 0.8113023683968590);
const mat3 AgXOutsetMatrix = mat3(
	1.1271005818144368, -0.1106066430966032, -0.0164939387178346,
	-0.1413297634984383,  1.1578237022162720, -0.0164939387178343,
	-0.1413297634984383, -0.1106066430966029,  1.2519364065950405);
const float AgXMinEV = -12.47393;
const float AgXMaxEV = 4.026069;

vec3 AgXDefaultContrastApprox(vec3 x)
{
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;
	return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 AgXTonemap(vec3 color)
{
	color = AgXInsetMatrix * color;
	color = max(color, 1e-10);
	color = clamp(log2(color), AgXMinEV, AgXMaxEV);
	color = (color - AgXMinEV) / (AgXMaxEV - AgXMinEV);
	color = AgXDefaultContrastApprox(color);

	// EOTF: back to a linear-ish range for the shared gamma encoding below.
	color = AgXOutsetMatrix * color;
	color = pow(max(color, 0.0), vec3(2.2));
	return color;
}

// Lift / Gamma / Gain colour grading (DaVinci-style), applied pre-tonemap in linear.
vec3 ApplyLiftGammaGain(vec3 color, vec3 lift, vec3 gamma, vec3 gain)
{
	color = color * gain + lift * (vec3(1.0) - color);
	color = pow(max(color, vec3(0.0)), vec3(1.0) / max(gamma, vec3(1e-3)));
	return color;
}

vec3 GammaCorrect(vec3 color, float gamma)
{
	return pow(color, vec3(1.0f / gamma));
}

void main()
{
	const float gamma     = max(u_Uniforms.ContrastGamma.y, 0.01);
	const float pureWhite = 1.0;
	float sampleScale = 0.5;

	vec3 color = SampleLinear(u_Texture, Input.TexCoord).rgb;

#if HZ_RENDERER_EDGE_OUTLINE_EFFECT
	{
		const vec4 edgeColor = vec4(0.2, 0.2, 0.15, 1.0);
		const vec4 backgroundColor = vec4(1,0.95,0.85,1);
		float errorPeriod = 30.0;
		float errorRange = 0.001;

		vec2 uvs[3];
		uvs[0] = Input.TexCoord + vec2(errorRange * sin(errorPeriod * Input.TexCoord.y + 0.0), errorRange * sin(errorPeriod * Input.TexCoord.x + 0.0));
		uvs[1] = Input.TexCoord + vec2(errorRange * sin(errorPeriod * Input.TexCoord.y + 1.047), errorRange * sin(errorPeriod * Input.TexCoord.x + 3.142));
		uvs[2] = Input.TexCoord + vec2(errorRange * sin(errorPeriod * Input.TexCoord.y + 2.094), errorRange * sin(errorPeriod * Input.TexCoord.x + 1.571));
    
		float edge = SampleLinear(u_EdgeTexture, uvs[0]).r * SampleLinear(u_EdgeTexture, uvs[1]).r * SampleLinear(u_EdgeTexture, uvs[2]).r;

		// float edge = texture(u_EdgeTexture, Input.TexCoord).r;
		//	color *= edge;

		if (edge < 0.5)
		{
			color = mix(color, edgeColor.rgb * 0.1, 0.9);
		}

		float diffuse = (color.x + color.y + color.z) / 3.0;

		float w = fwidth(diffuse) * 2.0;
		vec4 mCol = mix(backgroundColor * 0.5, backgroundColor, mix(0.0, 1.0, smoothstep(-w, w, diffuse - 0.3)));
		vec3 newCol = mix(edgeColor, mCol, edge).xyz;
		//color = mix(newCol, color, 0.0);
		edge += 0.5;
		edge = clamp(edge, 0.0, 1.0);
	}
#endif

	ivec2 texSize = GetTextureSize(u_BloomTexture, 0);
	vec2 fTexSize = vec2(float(texSize.x), float(texSize.y));
	vec3 bloom = UpsampleTent9(u_BloomTexture, 0, Input.TexCoord, 1.0f / fTexSize, sampleScale) * u_Uniforms.BloomIntensity;
	vec3 bloomDirt = SampleLinear(u_BloomDirtTexture, Input.TexCoord).rgb * u_Uniforms.BloomDirtIntensity;

	// Fog
#if HZ_RENDERER_FOG_EFFECT
	{
		float depth = SampleLinear(u_DepthTexture, Input.TexCoord).r;
		depth = LinearizeDepth(depth);

		float fogStartDistance = 5.5f;
		//fogStartDistance = 1.0f;
		float bloomFogStartDistance = 50.0f;
		float fogFallOffDistance = 30.0f;
		float bloomFogFallOffDistance = 50.0f;

		float fogAmount = smoothstep(fogStartDistance, fogStartDistance + fogFallOffDistance, depth);
		float fogAmountBloom = smoothstep(bloomFogStartDistance, bloomFogStartDistance + bloomFogFallOffDistance, depth);

		// Skybox
		//if (d == 0.0)
		//{
		//	color *= 0.2f;
		//	fogAmount = 0.0;
		//	fogAmountBloom = 0.0;
		//}

		vec3 fogColor = vec3(0.11f, 0.12f, 0.15f);
		fogColor *= 2.0f;
		vec3 bloomClamped = clamp(bloom * (1.0f - fogAmountBloom), 0.0f, 1.0f);
		float intensity = (bloomClamped.r + bloomClamped.g + bloomClamped.b) / 3.0f;
		fogColor = mix(fogColor, color, intensity);

		color = mix(color, fogColor, fogAmount);
		fogAmountBloom = clamp(fogAmountBloom, 0, 1);
		//bloom *= (1.0f - fogAmountBloom);
	}
#endif

	color += bloom;
	color += bloom * bloomDirt;
	float exposure = (u_Uniforms.UseAutoExposure != 0) ? b_Exposure.Exposure : u_Uniforms.Exposure;
	color *= exposure;
	color *= max(u_Uniforms.ColorFilterSaturation.rgb, vec3(0.0));

	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	color = mix(vec3(luminance), color, clamp(u_Uniforms.ColorFilterSaturation.a, 0.0, 2.0));
	color = max((color - vec3(0.18)) * max(u_Uniforms.ContrastGamma.x, 0.0) + vec3(0.18), vec3(0.0));

	// Grain
	float strength = 5.0;
    
    float x = (Input.TexCoord.x + 1.0 ) * (Input.TexCoord.y + 1.0 ) * u_Uniforms.Time;
	float grain = mod((mod(x, 13.0) + 1.0) * (mod(x, 123.0) + 1.0), 0.01) - 0.006;
    
    //color += grain * strength;

	// Colour grading: lift / gamma / gain (pre-tonemap, linear).
	color = ApplyLiftGammaGain(color, u_Uniforms.Lift.rgb, u_Uniforms.GradeGamma.rgb, u_Uniforms.Gain.rgb);

	// Tonemap (selectable operator). All operators return a ~linear result that the
	// shared display-gamma encoding below converts to display space.
	if (u_Uniforms.TonemapOperator == TONEMAP_AGX)
		color = AgXTonemap(color);
	else if (u_Uniforms.TonemapOperator == TONEMAP_NONE)
		color = clamp(color, 0.0, 1.0);
	else
		color = ACESTonemap(color);

	color = GammaCorrect(color.rgb, gamma);

	color *= u_Uniforms.Opacity;

	o_Color = vec4(color, 1.0);
}
