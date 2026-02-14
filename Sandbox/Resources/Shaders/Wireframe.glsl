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

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint _pad0;
	uint _pad1;
	uint _pad2;
	vec4 Color;
} u_MaterialUniforms;

void main()
{
	mat4 transform = GetInstanceTransform(u_MaterialUniforms.ObjectIndexBase + gl_InstanceIndex);
	gl_Position = u_Camera.ViewProjectionMatrix * transform * vec4(a_Position, 1.0);
}

#version 450 core
#pragma stage : frag

layout(location = 0) out vec4 color;

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint _pad0;
	uint _pad1;
	uint _pad2;
	vec4 Color;
} u_MaterialUniforms;

void main()
{
	color = u_MaterialUniforms.Color;
}
