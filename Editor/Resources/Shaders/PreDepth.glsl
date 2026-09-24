// Pre-depth shader

#version 450 core
#pragma stage : vert

#include <Buffers.glslh>

// Vertex buffer
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Binormal;
layout(location = 4) in vec2 a_TexCoord;

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

// Make sure both shaders compute the exact same answer(PBR shader).
// We need to have the same exact calculations to produce the gl_Position value (eg. matrix multiplications).
precise invariant gl_Position;

void main()
{
	uint objectIndex = u_PushConstants.ObjectIndexBase + gl_InstanceIndex;
	mat4 transform = GetInstanceTransform(objectIndex);
	vec4 worldPosition = transform * vec4(a_Position, 1.0);

	OutputTexCoord = a_TexCoord;
	OutputObjectIndex = objectIndex;
	OutputViewPosition = vec3(u_Camera.ViewMatrix * worldPosition);

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

layout(location = 0) in vec2 InputTexCoord;
layout(location = 1) flat in uint InputObjectIndex;
layout(location = 2) in vec3 InputViewPosition;

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

	vec2 uv = GetGPUMaterialUV(gpuMaterial, InputTexCoord);
	float alpha = SampleMaterialSceneTexture(gpuMaterial.TextureIndices.x, uv, GetMaterialMipBias()).a
		* GetGPUMaterialOpacity(gpuMaterial, 1.0);
	if (alpha < GetGPUMaterialAlphaCutoff(gpuMaterial, 0.5))
		discard;
}
