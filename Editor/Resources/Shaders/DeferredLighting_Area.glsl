// Area lights over deferred lighting: a full-screen pass blended additively into scene color, only
// in the graph on frames with area lights (SceneRenderer::AreaLightPass).
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

#include <Buffers.glslh>
#include <PBR.glslh>
#include <LuxGBuffer.glslh>
#include <LTC.glslh>

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;

layout(set = 1, binding = 12) uniform texture2D u_GBufferBaseColor;
layout(set = 1, binding = 13) uniform texture2D u_GBufferNormal;
layout(set = 1, binding = 14) uniform texture2D u_GBufferMetalRoughAO;
layout(set = 1, binding = 15) uniform utexture2D u_GBufferMaterialObjectID;
layout(set = 1, binding = 17) uniform texture2D u_DepthTexture;

void main()
{
	float depth = texture(sampler2D(u_DepthTexture, r_PointSampler), v_TexCoord).r;
	if (depth <= 0.000001)
		discard;

	vec4 world = u_Camera.InverseViewProjectionMatrix * vec4(v_ClipPosition, depth, 1.0);
	if (abs(world.w) <= 0.000001)
		discard;
	vec3 worldPosition = world.xyz / world.w;

	LuxGBufferData gbuffer = DecodeGBuffer(u_GBufferBaseColor, u_GBufferNormal, u_GBufferMetalRoughAO, u_GBufferMaterialObjectID, v_TexCoord);
	vec3 N = normalize(mat3(u_Camera.InverseViewMatrix) * gbuffer.ViewNormal);
	vec3 V = GetViewDirection(worldPosition);
	float roughness = max(gbuffer.Roughness, 0.05);
	float metalness = clamp(gbuffer.Metalness, 0.0, 1.0);
	vec3 F0 = mix(LuxDielectricF0(gbuffer.Specular), gbuffer.BaseColor, metalness);

	vec3 color = LuxEvaluateAreaLights(worldPosition, N, V, gbuffer.BaseColor, metalness, roughness, F0);
	o_Color = vec4(color, 1.0);
}
