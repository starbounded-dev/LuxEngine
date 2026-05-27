#version 450 core
#pragma stage : vert
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) out OutputBlock Output;

void main()
{
	vec4 position = vec4(a_Position.xy, 0.0, 1.0);
	Output.TexCoord = a_TexCoord;
	gl_Position = position;
}

#version 450 core
#pragma stage : frag

layout(location = 0) out vec4 o_Color;

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) in OutputBlock Input;

layout (set = 0, binding = 0) uniform texture2D u_Texture;
layout (set = 0, binding = 1) uniform sampler u_TextureSampler;

void main()
{
	o_Color = texture(sampler2D(u_Texture, u_TextureSampler), Input.TexCoord);
}
