#version 450 core
#pragma stage:vert

#include <Buffers.glslh>

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Binormal;
layout(location = 4) in vec2 a_TexCoord;

struct VertexOutput
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 TexCoord;
	mat3 WorldNormals;
	mat3 CameraView;
	vec3 ViewPosition;
};

layout(location = 0) out VertexOutput Output;
layout(location = 18) flat out uint OutputObjectIndex;
// TAA motion vectors: unjittered current + previous clip-space positions.
layout(location = 19) out vec4 OutputCurrentClip;
layout(location = 20) out vec4 OutputPreviousClip;

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} u_MaterialUniforms;

invariant gl_Position;

void main()
{
	uint objectIndex = u_MaterialUniforms.ObjectIndexBase + gl_InstanceIndex;
	mat4 transform = GetInstanceTransform(objectIndex);
	vec4 worldPosition = transform * vec4(a_Position, 1.0);

	mat4 previousTransform = GetPreviousInstanceTransform(objectIndex);
	vec4 previousWorldPosition = previousTransform * vec4(a_Position, 1.0);
	OutputCurrentClip = u_Camera.UnjitteredViewProjectionMatrix * worldPosition;
	OutputPreviousClip = u_Camera.PreviousViewProjectionMatrix * previousWorldPosition;

	OutputObjectIndex = objectIndex;
	Output.WorldPosition = worldPosition.xyz;
	Output.Normal = mat3(transform) * a_Normal;
	Output.TexCoord = vec2(a_TexCoord.x, 1.0 - a_TexCoord.y);
	Output.WorldNormals = mat3(transform) * mat3(a_Tangent, a_Binormal, a_Normal);
	Output.CameraView = mat3(u_Camera.ViewMatrix);
	Output.ViewPosition = vec3(u_Camera.ViewMatrix * vec4(Output.WorldPosition, 1.0));

	gl_Position = u_Camera.ViewProjectionMatrix * worldPosition;
}

#version 450 core

#extension GL_EXT_nonuniform_qualifier : enable

#pragma stage : frag

#include <Buffers.glslh>
#include <MaterialScene.glslh>
#include <Samplers.glslh>
#include <MaterialSurface.glslh>
#include <LuxGBuffer.glslh>

struct VertexOutput
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 TexCoord;
	mat3 WorldNormals;
	mat3 CameraView;
	vec3 ViewPosition;
};

layout(location = 0) in VertexOutput Input;
layout(location = 18) flat in uint InputObjectIndex;
layout(location = 19) in vec4 InputCurrentClip;
layout(location = 20) in vec4 InputPreviousClip;

layout(location = 0) out vec4 o_GBufferBaseColor;
layout(location = 1) out vec4 o_GBufferViewNormal;
layout(location = 2) out vec4 o_GBufferMetalRoughAO;
layout(location = 3) out uvec2 o_GBufferMaterialObjectID;
layout(location = 4) out vec2 o_GBufferVelocity;

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} u_MaterialUniforms;

vec4 ToLinear(vec4 sRGB)
{
	bvec3 cutoff = lessThan(sRGB.rgb, vec3(0.04045));
	vec3 higher = pow((sRGB.rgb + vec3(0.055)) / vec3(1.055), vec3(2.4));
	vec3 lower = sRGB.rgb / vec3(12.92);

	return vec4(mix(higher, lower, cutoff), sRGB.a);
}

float GetMaterialMipBias()
{
	float mipBias = u_RendererData.TextureMipBias;
	if (u_RendererData.EnableDistanceMipBias)
	{
		float distanceToCamera = length(Input.ViewPosition);
		float biasRange = max(u_RendererData.DistanceMipBiasEnd - u_RendererData.DistanceMipBiasStart, 1.0);
		float distanceFactor = clamp((distanceToCamera - u_RendererData.DistanceMipBiasStart) / biasRange, 0.0, 1.0);
		mipBias += distanceFactor * u_RendererData.DistanceMipBiasMax;
	}
	return mipBias;
}

void main()
{
	const float materialMipBias = GetMaterialMipBias();
	GPUMaterial gpuMaterial = GetGPUMaterialForObject(InputObjectIndex);

	vec3 materialBaseColor = GetGPUMaterialBaseColor(gpuMaterial, vec3(1.0));
	float materialOpacity = GetGPUMaterialOpacity(gpuMaterial, 1.0);
	float materialMetalness = GetGPUMaterialMetalness(gpuMaterial, 0.0);
	float materialRoughness = GetGPUMaterialRoughness(gpuMaterial, 0.5);
	uint materialAlphaMode = GetGPUMaterialAlphaMode(gpuMaterial, GPU_MATERIAL_ALPHA_OPAQUE);
	bool materialUseNormalMap = GetGPUMaterialUsesNormalMap(gpuMaterial, false);

	vec4 albedoTexColor = SampleMaterialSceneTexture(gpuMaterial.TextureIndices.x, Input.TexCoord, materialMipBias);
	vec3 baseColor = albedoTexColor.rgb * materialBaseColor;
	float alpha = albedoTexColor.a * materialOpacity;
	if (materialAlphaMode == GPU_MATERIAL_ALPHA_MASKED && alpha < 0.5)
		discard;

	float metalness = SampleMaterialSceneTexture(gpuMaterial.TextureIndices.z, Input.TexCoord, materialMipBias).b * materialMetalness;
	float roughness = SampleMaterialSceneTexture(gpuMaterial.TextureIndices.w, Input.TexCoord, materialMipBias).g * materialRoughness;
	roughness = max(roughness, 0.05);

	vec3 worldNormal = normalize(Input.Normal);
	if (materialUseNormalMap)
	{
		vec4 normalTexColor = SampleMaterialSceneTexture(gpuMaterial.TextureIndices.y, Input.TexCoord, materialMipBias);
		worldNormal = LuxApplyNormalMap(Input.Normal, Input.WorldNormals, normalTexColor.rgb);
	}

	LuxGBufferData gbuffer;
	gbuffer.BaseColor = baseColor;
	gbuffer.Opacity = alpha;
	gbuffer.ViewNormal = normalize(Input.CameraView * worldNormal);
	gbuffer.Reserved0 = 0.0;
	gbuffer.Metalness = metalness;
	gbuffer.Roughness = roughness;
	gbuffer.AmbientOcclusion = 1.0;
	gbuffer.Specular = 0.5;
	gbuffer.MaterialID = GetInstanceMaterialIndex(InputObjectIndex);
	gbuffer.ObjectID = GetInstancePrimitiveID(InputObjectIndex);

	EncodeGBuffer(gbuffer, o_GBufferBaseColor, o_GBufferViewNormal, o_GBufferMetalRoughAO, o_GBufferMaterialObjectID);

	// Screen-space motion vector (UV delta, current - previous), jitter removed.
	vec2 currentUV = (InputCurrentClip.xy / InputCurrentClip.w) * 0.5 + 0.5;
	vec2 previousUV = (InputPreviousClip.xy / InputPreviousClip.w) * 0.5 + 0.5;
	o_GBufferVelocity = currentUV - previousUV;
}
