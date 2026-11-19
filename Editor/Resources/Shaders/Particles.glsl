// Particles: camera-facing quads, one instanced draw per system
//
// A unit quad is drawn once per particle (docs/PARTICLES_PLAN.md). Each instance carries its world position, rotation,
// size, color (four half floats) and texture rectangle. The pass writes scene color only, so particles are HDR: bloom and
// exposure see them. There is no depth attachment: the depth test and the soft edge both come from the scene depth, read
// as a texture, so a particle fades out over SoftDistance where it meets geometry instead of cutting a hard line.

#version 450 core
#pragma stage : vert
#include <Buffers.glslh>

// Stream 0: the quad's corner, from (-0.5, -0.5) to (0.5, 0.5).
layout(location = 0) in vec2 a_Corner;
// Stream 1, per instance (Particles::Instance, 48 bytes).
layout(location = 1) in vec3 a_Position;
layout(location = 2) in float a_Rotation;
layout(location = 3) in vec2 a_Size;
layout(location = 4) in vec2 a_ColorRG;
layout(location = 5) in vec2 a_ColorBA;
layout(location = 6) in vec4 a_UVRect; // u0 v0 u1 v1, v up like Renderer2D

layout(push_constant) uniform Batch
{
	vec4 Params; // x texture slot (negative: none, draws a soft round dot), y soft distance, z 1 for additive, w 1 for premultiplied
} u_Batch;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec2 v_Local;
layout(location = 2) out vec4 v_Color;
layout(location = 3) out float v_ViewDepth;

void main()
{
	float s = sin(a_Rotation);
	float c = cos(a_Rotation);
	vec2 corner = vec2(c * a_Corner.x - s * a_Corner.y, s * a_Corner.x + c * a_Corner.y) * a_Size;

	// The camera's own axes, so the quad faces the camera for a perspective and an orthographic view alike.
	vec3 right = u_Camera.InverseViewMatrix[0].xyz;
	vec3 up = u_Camera.InverseViewMatrix[1].xyz;
	vec3 world = a_Position + right * corner.x + up * corner.y;

	v_TexCoord = mix(a_UVRect.xy, a_UVRect.zw, a_Corner + 0.5);
	v_Local = a_Corner * 2.0;
	v_Color = vec4(a_ColorRG, a_ColorBA);
	v_ViewDepth = -(u_Camera.ViewMatrix * vec4(world, 1.0)).z;
	gl_Position = u_Camera.ViewProjectionMatrix * vec4(world, 1.0);
}

#version 450 core
#pragma stage : frag
#include <Buffers.glslh>
#include <Samplers.glslh>

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_Local;
layout(location = 2) in vec4 v_Color;
layout(location = 3) in float v_ViewDepth;

layout(location = 0) out vec4 o_Color;

layout(push_constant) uniform Batch
{
	vec4 Params;
} u_Batch;

layout(set = 0, binding = 0) uniform texture2D u_ParticleTextures[16];
layout(set = 1, binding = 17) uniform texture2D u_DepthTexture;

void main()
{
	vec4 color = v_Color;
	int slot = int(u_Batch.Params.x);
	if (slot >= 0)
		color *= SampleDefault(u_ParticleTextures[slot], v_TexCoord);
	else
		color.a *= 1.0 - smoothstep(0.25, 1.0, length(v_Local)); // no texture: a soft round dot

	// The geometry behind the particle, in the same view-space depth as the particle.
	float sceneDepth = LinearizeDeviceDepth(texelFetch(sampler2D(u_DepthTexture, r_PointSampler), ivec2(gl_FragCoord.xy), 0).r);
	float soft = u_Batch.Params.y;
	float visible = soft > 0.0 ? clamp((sceneDepth - v_ViewDepth) / soft, 0.0, 1.0) : (v_ViewDepth <= sceneDepth ? 1.0 : 0.0);
	color.a *= visible;
	if (u_Batch.Params.w > 0.5)
		color.rgb *= visible; // premultiplied: the color carries the coverage too

	if (u_Batch.Params.z > 0.5)
		color.rgb *= color.a; // additive: light that is not covered adds nothing
	o_Color = color;
}
