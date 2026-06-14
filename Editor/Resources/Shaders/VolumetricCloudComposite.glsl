#version 450 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec2 v_ClipPosition;

void main()
{
	v_TexCoord = a_TexCoord;
	v_ClipPosition = a_Position.xy;
	gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core
#pragma stage : frag

#include <Samplers.glslh>
#include <Atmosphere.glslh>

layout(set = 1, binding = 0) uniform texture2D u_CloudTexture;
layout(set = 1, binding = 1) uniform texture2D u_DepthTexture;
layout(set = 1, binding = 2) uniform texture2D u_CloudDepthTexture;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;

const float LUX_CLOUD_MAX_DEPTH = 65000.0;

float GetSceneDistance()
{
	float depth = texture(sampler2D(u_DepthTexture, r_PointSampler), v_TexCoord).r;
	if (depth <= 0.000001)
		return LUX_CLOUD_MAX_DEPTH;

	vec3 cameraPosition = GetAtmosphereCameraPosition();
	vec3 scenePosition = ReconstructAtmosphereWorldPosition(v_ClipPosition, depth);
	return min(length(scenePosition - cameraPosition), LUX_CLOUD_MAX_DEPTH);
}

float ComputeDepthVisibility(float cloudFrontDistance, float sceneDistance)
{
	if (cloudFrontDistance >= LUX_CLOUD_MAX_DEPTH * 0.99)
		return 0.0;
	if (sceneDistance >= LUX_CLOUD_MAX_DEPTH * 0.99)
		return 1.0;

	float edgeWidth = max(35.0, cloudFrontDistance * 0.0125);
	return smoothstep(cloudFrontDistance - edgeWidth, cloudFrontDistance + edgeWidth, sceneDistance);
}

void AccumulateCloudTap(vec2 uv, float bilinearWeight, float sceneDistance, inout vec4 weightedCloud, inout float totalWeight)
{
	vec4 cloud = texture(sampler2D(u_CloudTexture, r_PointSampler), uv);
	if (cloud.a <= 0.0001)
		return;

	vec4 cloudDepth = texture(sampler2D(u_CloudDepthTexture, r_PointSampler), uv);
	float visibility = ComputeDepthVisibility(cloudDepth.x, sceneDistance);
	if (visibility <= 0.0001)
		return;

	float sceneDepthError = abs(cloudDepth.y - sceneDistance);
	float acceptanceDistance = max(120.0, min(sceneDistance, cloudDepth.y) * 0.08);
	float depthWeight = sceneDistance >= LUX_CLOUD_MAX_DEPTH * 0.99 ? 1.0 : exp(-sceneDepthError / acceptanceDistance);

	float weight = bilinearWeight * depthWeight * visibility;
	weightedCloud += vec4(cloud.rgb * cloud.a, cloud.a) * weight;
	totalWeight += weight;
}

vec4 ReconstructCloud(float sceneDistance)
{
	ivec2 cloudSizeI = textureSize(sampler2D(u_CloudTexture, r_PointSampler), 0);
	vec2 cloudSize = vec2(max(cloudSizeI, ivec2(1)));
	vec2 coord = v_TexCoord * cloudSize - vec2(0.5);
	vec2 base = floor(coord);
	vec2 fraction = clamp(coord - base, vec2(0.0), vec2(1.0));

	vec2 uv00 = (base + vec2(0.5, 0.5)) / cloudSize;
	vec2 uv10 = (base + vec2(1.5, 0.5)) / cloudSize;
	vec2 uv01 = (base + vec2(0.5, 1.5)) / cloudSize;
	vec2 uv11 = (base + vec2(1.5, 1.5)) / cloudSize;

	uv00 = clamp(uv00, vec2(0.0), vec2(1.0));
	uv10 = clamp(uv10, vec2(0.0), vec2(1.0));
	uv01 = clamp(uv01, vec2(0.0), vec2(1.0));
	uv11 = clamp(uv11, vec2(0.0), vec2(1.0));

	vec4 weightedCloud = vec4(0.0);
	float totalWeight = 0.0;
	AccumulateCloudTap(uv00, (1.0 - fraction.x) * (1.0 - fraction.y), sceneDistance, weightedCloud, totalWeight);
	AccumulateCloudTap(uv10, fraction.x * (1.0 - fraction.y), sceneDistance, weightedCloud, totalWeight);
	AccumulateCloudTap(uv01, (1.0 - fraction.x) * fraction.y, sceneDistance, weightedCloud, totalWeight);
	AccumulateCloudTap(uv11, fraction.x * fraction.y, sceneDistance, weightedCloud, totalWeight);

	if (totalWeight <= 0.0001 || weightedCloud.a <= 0.0001)
		return vec4(0.0);

	float alpha = clamp(weightedCloud.a / totalWeight, 0.0, 1.0);
	vec3 color = weightedCloud.rgb / max(weightedCloud.a, 0.0001);
	return vec4(color, alpha);
}

void main()
{
	if (u_Atmosphere.Flags.y == 0u)
	{
		o_Color = vec4(0.0);
		return;
	}

	vec4 cloud = ReconstructCloud(GetSceneDistance());
	o_Color = vec4(cloud.rgb, clamp(cloud.a, 0.0, 1.0));
}
