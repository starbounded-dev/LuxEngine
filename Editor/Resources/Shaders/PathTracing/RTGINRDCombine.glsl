#version 460 core
#pragma stage : comp
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 94) uniform texture2D u_RTGIAlbedoTex;
layout(set = 1, binding = 96) uniform texture2D u_RTGISceneColor;
layout(set = 1, binding = 185) uniform texture2D u_NRDDenoised;
layout(rgba16f, set = 1, binding = 97) uniform writeonly image2D o_RTGIOutput;

void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, imageSize(o_RTGIOutput))))
		return;

	vec4 scene = texelFetch(u_RTGISceneColor, pixel, 0);
	vec4 surface = texelFetch(u_RTGIAlbedoTex, pixel, 0);
	if (surface.a > 0.0)
		scene.rgb += surface.rgb * max(texelFetch(u_NRDDenoised, pixel, 0).rgb, vec3(0.0));
	imageStore(o_RTGIOutput, pixel, scene);
}
