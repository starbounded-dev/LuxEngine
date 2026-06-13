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

#extension GL_EXT_nonuniform_qualifier : enable

#pragma stage : frag

#include <Buffers.glslh>
#include <MaterialScene.glslh>
#include <PBR.glslh>
#include <Lighting.glslh>
#include <ShadowMapping.glslh>
#include <LuxGBuffer.glslh>

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;

layout(set = 1, binding = 0) uniform textureCube u_EnvRadianceTex;
layout(set = 1, binding = 1) uniform textureCube u_EnvIrradianceTex;
layout(set = 1, binding = 2) uniform texture2DArray u_ShadowMapTexture;
layout(set = 1, binding = 3) uniform texture2D u_SpotShadowTexture;
layout(set = 1, binding = 11) uniform texture2D u_SceneColor;
layout(set = 1, binding = 12) uniform texture2D u_GBufferBaseColor;
layout(set = 1, binding = 13) uniform texture2D u_GBufferNormal;
layout(set = 1, binding = 14) uniform texture2D u_GBufferMetalRoughAO;
layout(set = 1, binding = 15) uniform utexture2D u_GBufferMaterialID;
layout(set = 1, binding = 16) uniform utexture2D u_GBufferObjectID;
layout(set = 1, binding = 17) uniform texture2D u_DepthTexture;

layout(set = 3, binding = 5) uniform texture2D u_BRDFLUTTexture;

bool ReconstructPositionFromDepth(float deviceDepth, out vec3 worldPosition, out vec3 viewPosition)
{
	vec4 world = u_Camera.InverseViewProjectionMatrix * vec4(v_ClipPosition, deviceDepth, 1.0);
	if (abs(world.w) <= 0.000001)
		return false;

	worldPosition = world.xyz / world.w;
	viewPosition = (u_Camera.ViewMatrix * vec4(worldPosition, 1.0)).xyz;
	return true;
}

vec3 IBL(vec3 F0, vec3 Lr)
{
	return LuxEvaluateImageBasedLighting(
		u_EnvRadianceTex,
		u_EnvIrradianceTex,
		u_BRDFLUTTexture,
		m_Params.Normal,
		Lr,
		m_Params.Albedo,
		m_Params.Roughness,
		m_Params.Metalness,
		m_Params.NdotV,
		F0,
		0.0);
}

float CalculateDirectionalShadow(vec3 worldPosition, vec3 viewPosition)
{
	if (u_Scene.DirectionalLights.Multiplier <= 0.0)
		return 0.0;

	uint cascadeIndex = 0;
	for (uint i = 0; i < 3; i++)
	{
		if (viewPosition.z < u_RendererData.CascadeSplits[i])
			cascadeIndex = i + 1;
	}

	float shadowDistance = u_RendererData.MaxShadowDistance;
	float transitionDistance = max(u_RendererData.ShadowFade, 0.001);
	float distanceToCamera = length(viewPosition);
	ShadowFade = distanceToCamera - (shadowDistance - transitionDistance);
	ShadowFade = clamp(1.0 - (ShadowFade / transitionDistance), 0.0, 1.0);

	vec4 shadowCoordsClip[4];
	shadowCoordsClip[0] = u_DirShadow.DirLightMatrices[0] * vec4(worldPosition, 1.0);
	shadowCoordsClip[1] = u_DirShadow.DirLightMatrices[1] * vec4(worldPosition, 1.0);
	shadowCoordsClip[2] = u_DirShadow.DirLightMatrices[2] * vec4(worldPosition, 1.0);
	shadowCoordsClip[3] = u_DirShadow.DirLightMatrices[3] * vec4(worldPosition, 1.0);

	vec3 shadowCoords[4];
	shadowCoords[0] = shadowCoordsClip[0].xyz / shadowCoordsClip[0].w;
	shadowCoords[1] = shadowCoordsClip[1].xyz / shadowCoordsClip[1].w;
	shadowCoords[2] = shadowCoordsClip[2].xyz / shadowCoordsClip[2].w;
	shadowCoords[3] = shadowCoordsClip[3].xyz / shadowCoordsClip[3].w;

	float shadowScale;
	if (u_RendererData.CascadeFading)
	{
		float cascadeTransitionFade = u_RendererData.CascadeTransitionFade;
		float c0 = smoothstep(u_RendererData.CascadeSplits[0] + cascadeTransitionFade * 0.5, u_RendererData.CascadeSplits[0] - cascadeTransitionFade * 0.5, viewPosition.z);
		float c1 = smoothstep(u_RendererData.CascadeSplits[1] + cascadeTransitionFade * 0.5, u_RendererData.CascadeSplits[1] - cascadeTransitionFade * 0.5, viewPosition.z);
		float c2 = smoothstep(u_RendererData.CascadeSplits[2] + cascadeTransitionFade * 0.5, u_RendererData.CascadeSplits[2] - cascadeTransitionFade * 0.5, viewPosition.z);

		if (c0 > 0.0 && c0 < 1.0)
		{
			float s0 = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, 0, shadowCoords[0], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, 0, shadowCoords[0]);
			float s1 = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, 1, shadowCoords[1], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, 1, shadowCoords[1]);
			shadowScale = mix(s0, s1, c0);
		}
		else if (c1 > 0.0 && c1 < 1.0)
		{
			float s1 = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, 1, shadowCoords[1], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, 1, shadowCoords[1]);
			float s2 = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, 2, shadowCoords[2], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, 2, shadowCoords[2]);
			shadowScale = mix(s1, s2, c1);
		}
		else if (c2 > 0.0 && c2 < 1.0)
		{
			float s2 = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, 2, shadowCoords[2], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, 2, shadowCoords[2]);
			float s3 = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, 3, shadowCoords[3], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, 3, shadowCoords[3]);
			shadowScale = mix(s2, s3, c2);
		}
		else
		{
			shadowScale = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, cascadeIndex, shadowCoords[cascadeIndex], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, cascadeIndex, shadowCoords[cascadeIndex]);
		}
	}
	else
	{
		shadowScale = u_RendererData.SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTexture, cascadeIndex, shadowCoords[cascadeIndex], u_RendererData.LightSize) : HardShadows_DirectionalLight(u_ShadowMapTexture, cascadeIndex, shadowCoords[cascadeIndex]);
	}

	return 1.0 - clamp(u_Scene.DirectionalLights.ShadowAmount - shadowScale, 0.0, 1.0);
}

vec3 DebugGradient(float value)
{
	float step0 = 0.0;
	float step1 = 1.0;
	float step2 = 3.0;
	float step3 = 6.0;
	float step4 = 10.0;
	vec3 zero = vec3(0.0);
	vec3 white = vec3(1.0);
	vec3 red = vec3(1.0, 0.0, 0.0);
	vec3 blue = vec3(0.0, 0.0, 1.0);
	vec3 green = vec3(0.0, 1.0, 0.0);
	vec3 color = mix(zero, white, smoothstep(step0, step1, value));
	color = mix(color, red, smoothstep(step1, step2, value));
	color = mix(color, blue, smoothstep(step2, step3, value));
	color = mix(color, green, smoothstep(step3, step4, value));
	return color;
}

void main()
{
	float depth = texture(sampler2D(u_DepthTexture, r_PointSampler), v_TexCoord).r;
	if (depth <= 0.000001)
	{
		discard;
	}

	vec3 worldPosition;
	vec3 viewPosition;
	if (!ReconstructPositionFromDepth(depth, worldPosition, viewPosition))
	{
		discard;
	}

	LuxGBufferData gbuffer = DecodeGBuffer(u_GBufferBaseColor, u_GBufferNormal, u_GBufferMetalRoughAO, u_GBufferMaterialID, u_GBufferObjectID, v_TexCoord);
	vec3 worldNormal = normalize(mat3(u_Camera.InverseViewMatrix) * gbuffer.ViewNormal);

	m_Params.Albedo = gbuffer.BaseColor;
	m_Params.Roughness = max(gbuffer.Roughness, 0.05);
	m_Params.Metalness = clamp(gbuffer.Metalness, 0.0, 1.0);
	m_Params.Normal = worldNormal;
	m_Params.View = normalize(u_Scene.CameraPosition - worldPosition);
	m_Params.NdotV = max(dot(m_Params.Normal, m_Params.View), 0.0);

	vec3 F0 = mix(LuxDielectricF0(gbuffer.Specular), m_Params.Albedo, m_Params.Metalness);
	vec3 Lr = reflect(-m_Params.View, m_Params.Normal);

	float shadowScale = CalculateDirectionalShadow(worldPosition, viewPosition);
	vec3 directLighting = CalculateDirLights(F0) * shadowScale;
	directLighting += CalculatePointLights(F0, worldPosition);
	directLighting += CalculateSpotLightsShadowed(F0, worldPosition, u_SpotShadowTexture);

	float emission = 0.0;
	if (gbuffer.MaterialID != GPU_TEXTURE_INVALID_INDEX)
		emission = GetGPUMaterialEmission(r_GPUMaterials.Materials[gbuffer.MaterialID], 0.0);

	vec3 iblLighting = IBL(F0, Lr) * u_Scene.EnvironmentMapIntensity;
	vec3 color = (directLighting + iblLighting) * max(gbuffer.AmbientOcclusion, 0.0);
	color += m_Params.Albedo * emission;

	if (u_RendererData.ShowLightComplexity)
		color = (color * 0.2) + DebugGradient(float(GetPointLightCount() + GetSpotLightCount()));

	o_Color = vec4(color, 1.0);
}
