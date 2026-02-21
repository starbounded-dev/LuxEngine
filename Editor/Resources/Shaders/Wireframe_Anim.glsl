// Outline Shader

#version 450 core
#pragma stage : vert
#include <Buffers.glslh>

layout(location = 0) in vec3 a_Position;

//////////////////////////////////////////
// UNUSED
//////////////////////////////////////////
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Binormal;
layout(location = 4) in vec2 a_TexCoord;
//////////////////////////////////////////

// Bone influences
layout(location = 5) in ivec4 a_BoneIndices;
layout(location = 6) in vec4 a_BoneWeights;

layout(push_constant) uniform PushConstants
{
	// Vertex shader
	uint ObjectIndexBase;
	uint _pad;
	uint BoneBase;
	uint BoneStride;	

	// Fragment shader
	vec4 Color;
} u_MaterialUniforms;

void main()
{
	mat4 transform = GetInstanceTransform(u_MaterialUniforms.ObjectIndexBase + gl_InstanceIndex);

	mat4 boneTransform = r_BoneTransforms.BoneTransforms[u_MaterialUniforms.BoneBase + (gl_InstanceIndex * u_MaterialUniforms.BoneStride) + a_BoneIndices[0]] * a_BoneWeights[0];
	boneTransform     += r_BoneTransforms.BoneTransforms[u_MaterialUniforms.BoneBase + (gl_InstanceIndex * u_MaterialUniforms.BoneStride) + a_BoneIndices[1]] * a_BoneWeights[1];
	boneTransform     += r_BoneTransforms.BoneTransforms[u_MaterialUniforms.BoneBase + (gl_InstanceIndex * u_MaterialUniforms.BoneStride) + a_BoneIndices[2]] * a_BoneWeights[2];
	boneTransform     += r_BoneTransforms.BoneTransforms[u_MaterialUniforms.BoneBase + (gl_InstanceIndex * u_MaterialUniforms.BoneStride) + a_BoneIndices[3]] * a_BoneWeights[3];

	gl_Position = u_Camera.ViewProjectionMatrix * transform * boneTransform * vec4(a_Position, 1.0);
}

#version 450 core
#pragma stage : frag

layout(location = 0) out vec4 color;

layout(push_constant) uniform PushConstants
{
	// Vertex shader
	uint ObjectIndexBase;
	uint BoneBase;
	uint BoneStride;
	uint _pad;

	// Fragment shader
	vec4 Color;
} u_MaterialUniforms;

void main()
{
	color = u_MaterialUniforms.Color;
}
