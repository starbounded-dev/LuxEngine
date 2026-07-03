#version 430 core
#pragma stage : comp

#include <Samplers.glslh>

layout(binding = 0, rgba16f) restrict writeonly uniform image2D o_Image;
layout(binding = 1)  uniform texture2D u_Input;

layout(push_constant) uniform Uniforms
{
	int PrevLod;
    int Mode; //See defines below 
} u_Info;

#define MODE_COPY (0)
#define MODE_HORIZONTAL_GAUSSIAN (1)
#define MODE_VERTICAL_GAUSSIAN (2)

#define WEIGHT0 0.1749379741597446
#define WEIGHT1 0.16556904917484133
#define WEIGHT2 0.14036678002195038
#define WEIGHT3 0.106595183723336

vec4 GaussianHorizontal(vec2 uv, vec2 pixelSize)
{
	vec4 color = SampleLinear(u_Input, uv + pixelSize.x * vec2(-3.0f, 0.0f)) * WEIGHT3;
    color += SampleLinear(u_Input, uv + pixelSize.x * vec2(-2.0f, 0.0f)) * WEIGHT2;
    color += SampleLinear(u_Input, uv + pixelSize.x * vec2(-1.0f, 0.0f)) * WEIGHT1;
    color += SampleLinear(u_Input, uv) * WEIGHT0;
    color += SampleLinear(u_Input, uv + pixelSize.x * vec2( 1.0f, 0.0f)) * WEIGHT1;
    color += SampleLinear(u_Input, uv + pixelSize.x * vec2( 2.0f, 0.0f)) * WEIGHT2;
    color += SampleLinear(u_Input, uv + pixelSize.x * vec2( 3.0f, 0.0f)) * WEIGHT3;
	
	return color;
}

vec4 GaussianVertical(vec2 uv, vec2 pixelSize)
{
	vec4 color = SampleLinear(u_Input, uv + pixelSize.y * vec2(0.0f, -3.0f)) * WEIGHT3;
    color += SampleLinear(u_Input, uv + pixelSize.y * vec2(0.0f, -2.0f)) * WEIGHT2;
    color += SampleLinear(u_Input, uv + pixelSize.y * vec2(0.0f, -1.0f)) * WEIGHT1;
    color += SampleLinear(u_Input, uv) * WEIGHT0;
    color += SampleLinear(u_Input, uv + pixelSize.y * vec2(0.0f,  1.0f)) * WEIGHT1;
    color += SampleLinear(u_Input, uv + pixelSize.y * vec2(0.0f,  2.0f)) * WEIGHT2;
    color += SampleLinear(u_Input, uv + pixelSize.y * vec2(0.0f,  3.0f)) * WEIGHT3;
	return color;
}


layout(local_size_x = 16, local_size_y = 16) in;
void main()
{
	vec2 imgSize = imageSize(o_Image);
	ivec2 invocID = ivec2(gl_GlobalInvocationID);
	if (invocID.x >= int(imgSize.x) || invocID.y >= int(imgSize.y))
		return;

	vec2 pixelSize = 1.0f / imgSize;
	vec2 texCoords = invocID * pixelSize + pixelSize * 0.5;
	vec4 finalColor;
	if (u_Info.Mode == MODE_COPY)
	{
		finalColor = SampleLinear(u_Input, texCoords);
	}
	else if (u_Info.Mode == MODE_HORIZONTAL_GAUSSIAN)
	{
		finalColor = GaussianHorizontal(texCoords, pixelSize);
	}
	else if(u_Info.Mode == MODE_VERTICAL_GAUSSIAN)
	{
		finalColor = GaussianVertical(texCoords, pixelSize);
	}

	imageStore(o_Image, invocID, finalColor);
}




