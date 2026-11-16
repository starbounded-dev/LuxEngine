#version 450 core
#pragma stage:vert

#include <Buffers.glslh>

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_QTangent; // tangent frame, QTangent.glslh
layout(location = 2) in vec2 a_TexCoord;
// The attribute stream (Mesh.h VertexAttributes): vertex colour and a second UV set.
layout(location = 3) in vec4 a_Color;
layout(location = 4) in vec2 a_TexCoord1;

#include <QTangent.glslh>

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
layout(location = 19) out vec4 OutputVertexColor;
layout(location = 20) out vec2 OutputTexCoord1;

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
	const mat3 tbn = QTangent_ToTBN(a_QTangent); // columns: tangent, binormal, normal
	Output.Normal = mat3(transform) * tbn[2];
	Output.TexCoord = vec2(a_TexCoord.x, 1.0 - a_TexCoord.y);
	OutputVertexColor = a_Color;
	OutputTexCoord1 = vec2(a_TexCoord1.x, 1.0 - a_TexCoord1.y);
	Output.WorldNormals = mat3(transform) * tbn;
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
#include <MaterialMapping.glslh>
#include <MaterialGraph.glslh>
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
layout(location = 19) in vec4 InputVertexColor;
layout(location = 20) in vec2 InputTexCoord1;

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
	const float faceSign = gl_FrontFacing ? 1.0 : -1.0;
	const vec3 vertexNormal = Input.Normal * faceSign;
	LuxMaterialCoords coords = LuxMaterialMapping(gpuMaterial, Input.TexCoord, Input.WorldPosition, vertexNormal, InputObjectIndex);

	vec4 albedoTexColor = LuxSampleMaterial(coords, gpuMaterial.TextureIndices.x, materialMipBias);
	float detailMask = LuxDetailMask(gpuMaterial, coords, materialMipBias);
	vec3 baseColor = LuxApplyDetailAlbedo(gpuMaterial, albedoTexColor.rgb * materialBaseColor, Input.TexCoord, detailMask, materialMipBias);
	float alpha = albedoTexColor.a * materialOpacity;
	// A graph material is tested after its graph, which may set Opacity.
	if (materialAlphaMode == GPU_MATERIAL_ALPHA_MASKED && gpuMaterial.TransmissionTextureIndices.y == 0u && alpha < GetGPUMaterialAlphaCutoff(gpuMaterial, 0.5))
		discard;

	float metalness = SelectGPUMaterialChannel(LuxSampleMaterial(coords, gpuMaterial.TextureIndices.z, materialMipBias), channelSelects, GPU_MATERIAL_CHANNEL_SHIFT_METALNESS) * materialMetalness;
	float roughness = SelectGPUMaterialChannel(LuxSampleMaterial(coords, gpuMaterial.TextureIndices.w, materialMipBias), channelSelects, GPU_MATERIAL_CHANNEL_SHIFT_ROUGHNESS) * materialRoughness;
	roughness = max(roughness, 0.05);

	float occlusion = 1.0;
	if (HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_OCCLUSION_TEXTURE))
	{
		float occlusionSample = SelectGPUMaterialChannel(LuxSampleMaterial(coords, gpuMaterial.ExtraTextureIndices.y, materialMipBias), channelSelects, GPU_MATERIAL_CHANNEL_SHIFT_OCCLUSION);
		occlusion = mix(1.0, occlusionSample, gpuMaterial.Emissive.w);
	}

	// Only a two-sided material's pipeline draws back faces. They are lit as the side the camera
	// sees, glTF's double-sided rule: the normal and tangent frame flip.
	vec3 worldNormal = normalize(vertexNormal);
	if (materialUseNormalMap)
		worldNormal = LuxSampleMaterialNormal(coords, gpuMaterial.TextureIndices.y, materialMipBias, vertexNormal, Input.WorldNormals * faceSign, gpuMaterial.Surface.y);
	if (HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_HEIGHT_TEXTURE))
	{
		float height = LuxSampleMaterial(coords, gpuMaterial.ExtraTextureIndices.z, materialMipBias).r;
		worldNormal = PerturbNormalFromHeight(worldNormal, Input.WorldPosition, height, gpuMaterial.Surface.w);
	}
	worldNormal = LuxApplyDetailNormal(gpuMaterial, worldNormal, Input.WorldNormals * faceSign, Input.TexCoord, detailMask, materialMipBias);

	vec3 emissive = GetGPUMaterialEmissive(gpuMaterial);
	if (HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_EMISSIVE_TEXTURE))
		emissive *= LuxSampleMaterial(coords, gpuMaterial.ExtraTextureIndices.x, materialMipBias).rgb;
	float specular = GetGPUMaterialSpecular(gpuMaterial, 0.5);

#if LUX_MATERIAL_GRAPH_COUNT > 0
	// A graph material: its graph sets the values its output connects; the rest stay as above.
	uint graphIndex = gpuMaterial.TransmissionTextureIndices.y;
	if (graphIndex != 0u)
	{
		LuxGraphInput graphInput = LuxMakeGraphInput(gpuMaterial, Input.TexCoord, Input.WorldPosition, vertexNormal, Input.WorldNormals * faceSign, InputObjectIndex, materialMipBias);
		graphInput.VertexColor = InputVertexColor;
		graphInput.UV1 = InputTexCoord1;
		LuxGraphOutput graph = LuxGraphOutput(baseColor, metalness, roughness, emissive, alpha, occlusion, worldNormal, specular);
		LuxEvaluateMaterialGraph(graphIndex, graphInput, graph);
		baseColor = max(graph.BaseColor, vec3(0.0));
		metalness = clamp(graph.Metallic, 0.0, 1.0);
		roughness = clamp(graph.Roughness, 0.05, 1.0);
		emissive = max(graph.Emission, vec3(0.0));
		alpha = graph.Opacity;
		occlusion = clamp(graph.Occlusion, 0.0, 1.0);
		worldNormal = graph.Normal;
		specular = clamp(graph.Specular, 0.0, 1.0);
		if (materialAlphaMode == GPU_MATERIAL_ALPHA_MASKED && alpha < GetGPUMaterialAlphaCutoff(gpuMaterial, 0.5))
			discard;
	}
#endif

	o_Emissive = vec4(emissive, 1.0);

	LuxGBufferData gbuffer;
	gbuffer.BaseColor = baseColor;
	gbuffer.Opacity = alpha;
	gbuffer.ViewNormal = normalize(Input.CameraView * worldNormal);
	// Deferred lighting has no tangent, so an anisotropic material stores its own in the alpha.
	if (HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_ANISOTROPY))
	{
		// The mesh tangent turned by the material's rotation (as LuxAnisotropyTangent in MaterialLobes.glslh).
		vec3 tangent = cos(gpuMaterial.Lobes.w) * Input.WorldNormals[0] + sin(gpuMaterial.Lobes.w) * Input.WorldNormals[1];
		vec3 viewTangent = Input.CameraView * (tangent - worldNormal * dot(worldNormal, tangent));
		gbuffer.Opacity = EncodeGBufferTangent(gbuffer.ViewNormal, viewTangent);
	}
	// Deferred lighting reads the transmission colour from this material's row.
	float diffuseTransmission = gpuMaterial.Transmission.w;
	if (diffuseTransmission > 0.0 && HasGPUMaterialFlag(materialFlags, GPU_MATERIAL_FLAG_HAS_TRANSMISSION_TEXTURE))
		diffuseTransmission *= LuxSampleMaterial(coords, gpuMaterial.TransmissionTextureIndices.x, materialMipBias).a;
	gbuffer.DiffuseTransmission = diffuseTransmission;
	gbuffer.Metalness = metalness;
	gbuffer.Roughness = roughness;
	gbuffer.AmbientOcclusion = occlusion;
	gbuffer.Specular = specular;
	gbuffer.MaterialID = GetInstanceMaterialIndex(InputObjectIndex);
	gbuffer.ObjectID = GetInstancePrimitiveID(InputObjectIndex);

	EncodeGBuffer(gbuffer, o_GBufferBaseColor, o_GBufferViewNormal, o_GBufferMetalRoughAO, o_GBufferMaterialObjectID);
}
