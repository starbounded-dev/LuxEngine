#version 450 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_TexCoord;

void main()
{
	v_TexCoord = a_TexCoord;
	gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core

#extension GL_EXT_nonuniform_qualifier : enable

#pragma stage : frag

#include <Buffers.glslh>
#include <MaterialScene.glslh>
#include <LuxGBuffer.glslh>

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 o_Color;

layout(set = 1, binding = 0) uniform texture2D u_GBufferBaseColor;
layout(set = 1, binding = 1) uniform texture2D u_GBufferNormal;
layout(set = 1, binding = 2) uniform texture2D u_GBufferMetalRoughAO;
layout(set = 1, binding = 3) uniform utexture2D u_GBufferMaterialID;
layout(set = 1, binding = 4) uniform utexture2D u_GBufferObjectID;
layout(set = 1, binding = 5) uniform texture2D u_DeferredLighting;

layout(push_constant) uniform Uniforms
{
	uint Mode;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} u_Uniforms;

const uint GBUFFER_DEBUG_BASE_COLOR = 1u;
const uint GBUFFER_DEBUG_NORMAL = 2u;
const uint GBUFFER_DEBUG_METAL_ROUGH = 3u;
const uint GBUFFER_DEBUG_MATERIAL_ID = 4u;
const uint GBUFFER_DEBUG_OBJECT_ID = 5u;
const uint GBUFFER_DEBUG_DEFERRED_LIGHTING = 6u;
const uint GBUFFER_DEBUG_GPU_MATERIAL_TEXTURE_VALIDITY = 7u;
const uint GBUFFER_DEBUG_GPU_MATERIAL_ALPHA_MODE = 8u;
const uint GBUFFER_DEBUG_GPU_MATERIAL_ROUGHNESS = 9u;
const uint GBUFFER_DEBUG_GPU_MATERIAL_METALNESS = 10u;
const uint GBUFFER_DEBUG_GPU_MATERIAL_MISSING = 11u;

void main()
{
	LuxGBufferData gbuffer = DecodeGBuffer(u_GBufferBaseColor, u_GBufferNormal, u_GBufferMetalRoughAO, u_GBufferMaterialID, u_GBufferObjectID, v_TexCoord);

	if (u_Uniforms.Mode == GBUFFER_DEBUG_BASE_COLOR)
	{
		o_Color = vec4(gbuffer.BaseColor, 1.0);
		return;
	}

	if (u_Uniforms.Mode == GBUFFER_DEBUG_NORMAL)
	{
		o_Color = vec4(normalize(gbuffer.ViewNormal) * 0.5 + 0.5, 1.0);
		return;
	}

	if (u_Uniforms.Mode == GBUFFER_DEBUG_METAL_ROUGH)
	{
		o_Color = vec4(gbuffer.Metalness, gbuffer.Roughness, 0.0, 1.0);
		return;
	}

	if (u_Uniforms.Mode == GBUFFER_DEBUG_MATERIAL_ID)
	{
		o_Color = vec4(GBufferDebugColorFromID(gbuffer.MaterialID + 1u), 1.0);
		return;
	}

	if (u_Uniforms.Mode == GBUFFER_DEBUG_OBJECT_ID)
	{
		o_Color = vec4(GBufferDebugColorFromID(gbuffer.ObjectID + 1u), 1.0);
		return;
	}

	if (u_Uniforms.Mode == GBUFFER_DEBUG_DEFERRED_LIGHTING)
	{
		o_Color = texture(sampler2D(u_DeferredLighting, r_PointSampler), v_TexCoord);
		return;
	}

	uint materialDebugMode = GPU_MATERIAL_DEBUG_MISSING;
	if (u_Uniforms.Mode == GBUFFER_DEBUG_GPU_MATERIAL_TEXTURE_VALIDITY)
		materialDebugMode = GPU_MATERIAL_DEBUG_TEXTURE_VALIDITY;
	else if (u_Uniforms.Mode == GBUFFER_DEBUG_GPU_MATERIAL_ALPHA_MODE)
		materialDebugMode = GPU_MATERIAL_DEBUG_ALPHA_MODE;
	else if (u_Uniforms.Mode == GBUFFER_DEBUG_GPU_MATERIAL_ROUGHNESS)
		materialDebugMode = GPU_MATERIAL_DEBUG_ROUGHNESS;
	else if (u_Uniforms.Mode == GBUFFER_DEBUG_GPU_MATERIAL_METALNESS)
		materialDebugMode = GPU_MATERIAL_DEBUG_METALNESS;

	o_Color = vec4(GetMaterialSceneDebugColorForMaterialID(gbuffer.MaterialID, materialDebugMode), 1.0);
}
