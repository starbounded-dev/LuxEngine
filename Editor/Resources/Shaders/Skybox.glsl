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

	// Perspective: the unprojected far-plane point is a direction. Orthographic views look along
	// one axis, so the whole sky is the camera's forward direction.
	if (u_Camera.IsOrthographic != 0)
		v_Position = -u_Camera.InverseViewMatrix[2].xyz;
	else
		v_Position = (u_Camera.InverseViewProjectionMatrix * position).xyz;
}

#version 450 core
#pragma stage : frag

#include <Common.glslh>
#include <Samplers.glslh>

layout(location = 0) out vec4 finalColor;

layout (set = 0, binding = 1) uniform textureCube u_Texture;

layout (push_constant) uniform Uniforms
{
	float TextureLod;
	float Intensity;
	float Rotation; // degrees about +Y, matching the IBL rotation in the lighting passes
} u_Uniforms;

layout (location = 0) in vec3 v_Position;

void main()
{
	vec3 direction = RotateVectorAboutY(u_Uniforms.Rotation, v_Position);
	finalColor = SampleLinearLOD(u_Texture, direction, u_Uniforms.TextureLod) * u_Uniforms.Intensity;
	finalColor.a = 1.0f;
}
