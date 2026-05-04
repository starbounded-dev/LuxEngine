// Skybox shader

#version 450 core
#pragma stage : vert
#include <Buffers.glslh>

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec3 v_Position;

void main()
{
	vec4 position = vec4(a_Position.xy, 0.0, 1.0);
	gl_Position = position;

	v_Position = (u_Camera.InverseViewProjectionMatrix * position).xyz;
}

#version 450 core
#pragma stage : frag

#include <Samplers.glslh>

layout(location = 0) out vec4 finalColor;

layout (set = 0, binding = 1) uniform textureCube u_Texture;

layout (push_constant) uniform Uniforms
{
	float TextureLod;
	float Intensity;
} u_Uniforms;

layout (location = 0) in vec3 v_Position;

void main()
{
	vec3 sampleDirection = vec3(v_Position.x, -v_Position.y, v_Position.z);
	finalColor = SampleLinearLOD(u_Texture, sampleDirection, u_Uniforms.TextureLod) * u_Uniforms.Intensity;
	finalColor.a = 1.0f;
}
