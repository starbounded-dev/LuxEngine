// Spot Shadow Map shader

#version 450 core
#pragma stage : vert

#include <Buffers.glslh>

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Binormal;
layout(location = 4) in vec2 a_TexCoord;

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	int LightIndex;
	uint _pad0;
	uint _pad1;
} u_Renderer;

void main()
{
	mat4 transform = GetInstanceTransform(u_Renderer.ObjectIndexBase + gl_InstanceIndex);
	gl_Position = u_SpotLightMatrices.Mats[u_Renderer.LightIndex] * transform * vec4(a_Position, 1.0);
}

#version 450 core
#pragma stage : frag

void main()
{
	// TODO: Check for alpha in texture
}
