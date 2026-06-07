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

layout(set = 1, binding = 0) uniform texture2D u_CloudTexture;
layout(set = 1, binding = 1) uniform texture2D u_DepthTexture;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;

bool IntersectCompositeCloudLayer(vec3 origin, vec3 direction, out float t0, out float t1)
{
	float bottom = u_Atmosphere.CloudParams0.z;
	float top = bottom + max(u_Atmosphere.CloudParams0.w, 1.0);

	if (abs(direction.y) < 0.0001)
		return false;

	float a = (bottom - origin.y) / direction.y;
	float b = (top - origin.y) / direction.y;
	t0 = max(min(a, b), 0.0);
	t1 = max(a, b);
	return t1 > t0;
}

float ComputeCloudDepthVisibility()
{
	float t0;
	float t1;
	vec3 cameraPosition = GetAtmosphereCameraPosition();
	vec3 viewDirection = GetAtmosphereViewDirection(v_ClipPosition);
	if (!IntersectCompositeCloudLayer(cameraPosition, viewDirection, t0, t1))
		return 0.0;

	float depth = texture(sampler2D(u_DepthTexture, r_PointSampler), v_TexCoord).r;
	if (depth <= 0.000001)
		return 1.0;

	vec3 scenePosition = ReconstructAtmosphereWorldPosition(v_ClipPosition, depth);
	float sceneDistance = length(scenePosition - cameraPosition);
	float edgeWidth = max(40.0, t0 * 0.01);
	return smoothstep(t0 + edgeWidth, t0 + edgeWidth * 4.0, sceneDistance);
}

void main()
{
	if (u_Atmosphere.Flags.y == 0u)
	{
		o_Color = vec4(0.0);
		return;
	}

	vec4 cloud = texture(sampler2D(u_CloudTexture, r_LinearSampler), v_TexCoord);
	cloud.a *= ComputeCloudDepthVisibility();
	o_Color = vec4(cloud.rgb, clamp(cloud.a, 0.0, 1.0));
}
