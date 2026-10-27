// Pre-depth shader for Cutout materials: PreDepth plus the alpha test GBuffer_Static applies, so the
// two passes discard the same texels. Only alpha-masked draws (MeshKey::AlphaMasked) use it; the
// rest take PreDepth, which has no fragment work and keeps early depth testing.

#version 450 core
#pragma stage : vert

#include <Buffers.glslh>

// Vertex buffer
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_QTangent; // tangent frame, QTangent.glslh
layout(location = 2) in vec2 a_TexCoord;

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} u_PushConstants;

// Cutout materials must discard here too, or they punch full depth and occlude whatever is
// behind the transparent parts of the texture.
layout(location = 0) out vec2 OutputTexCoord;
layout(location = 1) flat out uint OutputObjectIndex;
layout(location = 2) out vec3 OutputViewPosition;
// What triplanar mapping and graphs read, computed as GBuffer_Static's vertex stage does.
layout(location = 3) out vec3 OutputWorldPosition;
layout(location = 4) out vec3 OutputNormal;
layout(location = 5) out mat3 OutputWorldNormals;

#include <QTangent.glslh>

// Make sure both shaders compute the exact same answer(PBR shader).
// We need to have the same exact calculations to produce the gl_Position value (eg. matrix multiplications).
precise invariant gl_Position;

void main()
{
	uint objectIndex = u_PushConstants.ObjectIndexBase + gl_InstanceIndex;
	mat4 transform = GetInstanceTransform(objectIndex);
	vec4 worldPosition = transform * vec4(a_Position, 1.0);

	// The UVs GBuffer_Static samples, or the two passes discard different texels.
	OutputTexCoord = vec2(a_TexCoord.x, 1.0 - a_TexCoord.y);
	OutputObjectIndex = objectIndex;
	OutputViewPosition = vec3(u_Camera.ViewMatrix * worldPosition);
	OutputWorldPosition = worldPosition.xyz;
	const mat3 tbn = QTangent_ToTBN(a_QTangent);
	OutputNormal = mat3(transform) * tbn[2];
	OutputWorldNormals = mat3(transform) * tbn;

    gl_Position = u_Camera.ViewProjectionMatrix * worldPosition;
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
layout(location = 2) in vec3 InputViewPosition;
layout(location = 3) in vec3 InputWorldPosition;
layout(location = 4) in vec3 InputNormal;
layout(location = 5) in mat3 InputWorldNormals;

// Must stay identical to GBuffer_Static's GetMaterialMipBias: the two passes have to sample the
// same mip, or they discard different fragments and the G-buffer fails its depth-equal test.
float GetMaterialMipBias()
{
	float mipBias = u_RendererData.TextureMipBias;
	if (u_RendererData.EnableDistanceMipBias)
	{
		float distanceToCamera = length(InputViewPosition);
		float biasRange = max(u_RendererData.DistanceMipBiasEnd - u_RendererData.DistanceMipBiasStart, 1.0);
		float distanceFactor = clamp((distanceToCamera - u_RendererData.DistanceMipBiasStart) / biasRange, 0.0, 1.0);
		mipBias += distanceFactor * u_RendererData.DistanceMipBiasMax;
	}
	return mipBias;
}

void main()
{
	GPUMaterial gpuMaterial = GetGPUMaterialForObject(InputObjectIndex);
	if (GetGPUMaterialAlphaMode(gpuMaterial, GPU_MATERIAL_ALPHA_OPAQUE) != GPU_MATERIAL_ALPHA_MASKED)
		return;

	const float faceSign = gl_FrontFacing ? 1.0 : -1.0;
	float alpha = LuxMaterialCutoutOpacity(gpuMaterial, InputTexCoord, InputWorldPosition, InputNormal * faceSign,
		InputWorldNormals * faceSign, InputObjectIndex, GetMaterialMipBias());
	if (alpha < GetGPUMaterialAlphaCutoff(gpuMaterial, 0.5))
		discard;
}
