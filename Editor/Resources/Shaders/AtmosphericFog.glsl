#version 450 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec2 v_ClipPosition;

void main()
{
	v_TexCoord = a_TexCoord;
	v_ClipPosition = a_Position.xy;
	gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core
#pragma stage : frag

#include <Samplers.glslh>
#include <Atmosphere.glslh>

layout(set = 1, binding = 1) uniform texture2D u_DepthTexture;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;

layout(push_constant) uniform Uniforms
{
	uint DebugMode;
} u_Uniforms;

void main()
{
	if (u_Atmosphere.Flags.z == 0u && u_Atmosphere.LocalFogParams.x == 0u)
	{
		o_Color = vec4(0.0);
		return;
	}

	float depth = texture(sampler2D(u_DepthTexture, r_PointSampler), v_TexCoord).r;
	vec3 viewDirection = GetAtmosphereViewDirection(v_ClipPosition);
	vec3 cameraPosition = GetAtmosphereCameraPosition();

	vec3 worldPosition;
	float distanceToCamera;
	if (depth > 0.000001)
	{
		worldPosition = ReconstructAtmosphereWorldPosition(v_ClipPosition, depth);
		distanceToCamera = length(worldPosition - cameraPosition);
	}
	else
	{
		distanceToCamera = max(u_Atmosphere.FogParams0.w, 1000.0);
		worldPosition = cameraPosition + viewDirection * distanceToCamera;
	}

	if (u_Uniforms.DebugMode == 1u)
	{
		float localDensity = EvaluateLocalFogAtPosition(worldPosition, viewDirection, distanceToCamera).a;
		o_Color = vec4(vec3(clamp(localDensity * 8.0, 0.0, 1.0)), 1.0);
		return;
	}

	vec4 fog = EvaluateHeightFog(worldPosition, viewDirection, distanceToCamera, v_ClipPosition);
	o_Color = vec4(fog.rgb, fog.a);
}
