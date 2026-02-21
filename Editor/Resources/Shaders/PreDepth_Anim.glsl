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

// Bone influences
layout(location = 5) in ivec4 a_BoneIndices;
layout(location = 6) in vec4 a_BoneWeights;

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint _pad;
	uint BoneBase;
	uint BoneStride;
} u_PushConstants;

// Make sure both shaders compute the exact same answer(PBR shader).
// We need to have the same exact calculations to produce the gl_Position value (eg. matrix multiplications).
precise invariant gl_Position;

void main()
{
	mat4 transform = GetInstanceTransform(u_PushConstants.ObjectIndexBase + gl_InstanceIndex);

	mat4 boneTransform = r_BoneTransforms.BoneTransforms[u_PushConstants.BoneBase + (gl_InstanceIndex * u_PushConstants.BoneStride) + a_BoneIndices[0]] * a_BoneWeights[0];
	boneTransform     += r_BoneTransforms.BoneTransforms[u_PushConstants.BoneBase + (gl_InstanceIndex * u_PushConstants.BoneStride) + a_BoneIndices[1]] * a_BoneWeights[1];
	boneTransform     += r_BoneTransforms.BoneTransforms[u_PushConstants.BoneBase + (gl_InstanceIndex * u_PushConstants.BoneStride) + a_BoneIndices[2]] * a_BoneWeights[2];
	boneTransform     += r_BoneTransforms.BoneTransforms[u_PushConstants.BoneBase + (gl_InstanceIndex * u_PushConstants.BoneStride) + a_BoneIndices[3]] * a_BoneWeights[3];

	vec4 worldPosition = transform * boneTransform * vec4(a_Position, 1.0);

	// Near and far are flipped for better precision.
	// Only change along with the PBR shader.
	gl_Position = u_Camera.ViewProjectionMatrix * worldPosition;
}

#version 450 core
#pragma stage : frag

void main()
{
	// TODO: Check for alpha in texture
}
