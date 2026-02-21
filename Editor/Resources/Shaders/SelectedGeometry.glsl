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

void main()
{
	mat4 transform = GetInstanceTransform(u_PushConstants.ObjectIndexBase + gl_InstanceIndex);
	gl_Position = u_Camera.ViewProjectionMatrix * transform * vec4(a_Position, 1.0);
}

#version 450 core
#pragma stage : frag

layout(location = 0) out vec4 o_Color;

void main()
{
	o_Color = vec4(1.0f);
}
