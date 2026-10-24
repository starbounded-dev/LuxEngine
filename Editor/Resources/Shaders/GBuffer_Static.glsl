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

layout(location = 0) out vec4 o_GBufferBaseColor;
layout(location = 1) out vec4 o_GBufferViewNormal;
layout(location = 2) out vec4 o_GBufferMetalRoughAO;
layout(location = 3) out uvec2 o_GBufferMaterialObjectID;
// Written straight into scene color; deferred lighting then adds the lit result on top.
layout(location = 4) out vec4 o_Emissive;

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
	uint materialFlags = gpuMaterial.Metadata.x;
	uint channelSelects = gpuMaterial.ExtraTextureIndices.w;
	vec2 uv = GetGPUMaterialUV(gpuMaterial, Input.TexCoord);

	vec4 albedoTexColor = SampleMaterialSceneTexture(gpuMaterial.TextureIndices.x, uv, materialMipBias);
	vec3 baseColor = albedoTexColor.rgb * materialBaseColor;
	float alpha = albedoTexColor.a * materialOpacity;
	if (materialAlphaMode == GPU_MATERIAL_ALPHA_MASKED && alpha < GetGPUMaterialAlphaCutoff(gpuMaterial, 0.5))
		discard;

	float metalness = SelectGPUMaterialChannel(SampleMaterialSceneTexture(gpuMaterial.TextureIndices.z, uv, materialMipBias), channelSelects, GPU_MATERIAL_CHANNEL_SHIFT_METALNESS) * materialMetalness;
	float roughness = SelectGPUMaterialChannel(SampleMaterialSceneTexture(gpuMaterial.TextureIndices.w, uv, materialMipBias), channelSelects, GPU_MATERIAL_CHANNEL_SHIFT_ROUGHNESS) * materialRoughness;
	roughness = max(roughness, 0.05);

	float occlusion = 1.0;
	if (HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_OCCLUSION_TEXTURE))
	{
		float occlusionSample = SelectGPUMaterialChannel(SampleMaterialSceneTexture(gpuMaterial.ExtraTextureIndices.y, uv, materialMipBias), channelSelects, GPU_MATERIAL_CHANNEL_SHIFT_OCCLUSION);
		occlusion = mix(1.0, occlusionSample, gpuMaterial.Emissive.w);
	}

	// Only a two-sided material's pipeline draws back faces. They are lit as the side the camera
	// sees, glTF's double-sided rule: the normal and tangent frame flip.
	const float faceSign = gl_FrontFacing ? 1.0 : -1.0;
	const vec3 vertexNormal = Input.Normal * faceSign;
	vec3 worldNormal = normalize(vertexNormal);
	if (materialUseNormalMap)
	{
		vec4 normalTexColor = SampleMaterialSceneTexture(gpuMaterial.TextureIndices.y, uv, materialMipBias);
		worldNormal = LuxApplyNormalMap(vertexNormal, Input.WorldNormals * faceSign, normalTexColor.rgb, gpuMaterial.Surface.y);
	}
	if (HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_HEIGHT_TEXTURE))
	{
		float height = SampleMaterialSceneTexture(gpuMaterial.ExtraTextureIndices.z, uv, materialMipBias).r;
		worldNormal = PerturbNormalFromHeight(worldNormal, Input.WorldPosition, height, gpuMaterial.Surface.w);
	}

	vec3 emissive = GetGPUMaterialEmissive(gpuMaterial);
	if (HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_EMISSIVE_TEXTURE))
		emissive *= SampleMaterialSceneTexture(gpuMaterial.ExtraTextureIndices.x, uv, materialMipBias).rgb;
	o_Emissive = vec4(emissive, 1.0);

	LuxGBufferData gbuffer;
	gbuffer.BaseColor = baseColor;
	gbuffer.Opacity = alpha;
	gbuffer.ViewNormal = normalize(Input.CameraView * worldNormal);
	// Deferred lighting reads the transmission colour from this material's row.
	float diffuseTransmission = gpuMaterial.Transmission.w;
	if (diffuseTransmission > 0.0 && HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_TRANSMISSION_TEXTURE))
		diffuseTransmission *= SampleMaterialSceneTexture(gpuMaterial.TransmissionTextureIndices.x, uv, materialMipBias).a;
	gbuffer.DiffuseTransmission = diffuseTransmission;
	gbuffer.Metalness = metalness;
	gbuffer.Roughness = roughness;
	gbuffer.AmbientOcclusion = occlusion;
	gbuffer.Specular = GetGPUMaterialSpecular(gpuMaterial, 0.5);
	gbuffer.MaterialID = GetInstanceMaterialIndex(InputObjectIndex);
	gbuffer.ObjectID = GetInstancePrimitiveID(InputObjectIndex);

	EncodeGBuffer(gbuffer, o_GBufferBaseColor, o_GBufferViewNormal, o_GBufferMetalRoughAO, o_GBufferMaterialObjectID);
}
