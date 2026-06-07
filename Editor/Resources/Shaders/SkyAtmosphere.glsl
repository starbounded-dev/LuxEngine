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

#include <Atmosphere.glslh>

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;

void main()
{
	if (u_Atmosphere.Flags.x == 0u)
		discard;

	vec3 viewDirection = GetAtmosphereViewDirection(v_ClipPosition);
	vec3 skyColor = EvaluateSkyAtmosphere(viewDirection);
	o_Color = vec4(skyColor, 1.0);
}
