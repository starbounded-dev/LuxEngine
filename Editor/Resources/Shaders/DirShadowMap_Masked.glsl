// Shadow Map shader for Cutout materials: DirShadowMap plus the alpha test the pre-depth and
// G-buffer passes apply, so a leaf card casts the leaf rather than the card. Only alpha-masked
// casters draw with it (MeshKey::AlphaMasked); the rest keep DirShadowMap, whose fragment stage
// does no work.

#version 450 core
#pragma stage : vert

#include <Buffers.glslh>

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_QTangent; // tangent frame, QTangent.glslh
layout(location = 2) in vec2 a_TexCoord;

layout (push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint Cascade;
	uint _pad0;
	uint _pad1;
} u_PushConstants;

layout(location = 0) out vec2 OutputTexCoord;
layout(location = 1) flat out uint OutputObjectIndex;
// What triplanar mapping and graphs read, as in GBuffer_Static.
layout(location = 2) out vec3 OutputWorldPosition;
layout(location = 3) out vec3 OutputNormal;
layout(location = 4) out mat3 OutputWorldNormals;

#include <QTangent.glslh>

void main()
{
	uint objectIndex = u_PushConstants.ObjectIndexBase + gl_InstanceIndex;
	mat4 transform = GetInstanceTransform(objectIndex);

	// The UVs GBuffer_Static samples, so the shadow is cut from the mask that is drawn.
	OutputTexCoord = vec2(a_TexCoord.x, 1.0 - a_TexCoord.y);
	OutputObjectIndex = objectIndex;
	OutputWorldPosition = (transform * vec4(a_Position, 1.0)).xyz;
	const mat3 tbn = QTangent_ToTBN(a_QTangent);
	OutputNormal = mat3(transform) * tbn[2];
	OutputWorldNormals = mat3(transform) * tbn;
	gl_Position = u_DirShadow.DirLightMatrices[u_PushConstants.Cascade] * transform * vec4(a_Position, 1.0);
}

#version 450 core

// Samplers.glslh indexes the bindless material texture array with nonuniformEXT; every shader
// that samples it declares the extension itself (the headers do not).
#extension GL_EXT_nonuniform_qualifier : enable

#pragma stage : frag

#include <Buffers.glslh>
#include <MaterialScene.glslh>
#include <Samplers.glslh>
#include <MaterialCutout.glslh>

layout(location = 0) in vec2 InputTexCoord;
layout(location = 1) flat in uint InputObjectIndex;
layout(location = 2) in vec3 InputWorldPosition;
layout(location = 3) in vec3 InputNormal;
layout(location = 4) in mat3 InputWorldNormals;

void main()
{
	// A material edited out of Cutout draws here until its bucket follows; it stays solid.
	GPUMaterial gpuMaterial = GetGPUMaterialForObject(InputObjectIndex);
	if (GetGPUMaterialAlphaMode(gpuMaterial, GPU_MATERIAL_ALPHA_OPAQUE) != GPU_MATERIAL_ALPHA_MASKED)
		return;

	// The mip the light's view of the texture needs; nothing here has to match the camera passes.
	// Shadows render without culling, so the face sign is the one the camera passes see for this side.
	const float faceSign = gl_FrontFacing ? 1.0 : -1.0;
	float alpha = LuxMaterialCutoutOpacity(gpuMaterial, InputTexCoord, InputWorldPosition, InputNormal * faceSign,
		InputWorldNormals * faceSign, InputObjectIndex, 0.0);
	if (alpha < GetGPUMaterialAlphaCutoff(gpuMaterial, 0.5))
		discard;
}
