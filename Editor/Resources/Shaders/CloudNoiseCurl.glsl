// Bakes the volumetric-cloud curl-noise volume.
//
// Output: 32^3 RGBA16F, tileable. RGB = a divergence-free (curl) vector field in
// [-1,1]; the raymarcher uses it to advect the detail-erosion sample position so
// cloud bases swirl into wispy tendrils (Nubis / Decima flavour). A is unused.

#version 450 core
#pragma stage : comp

#include <CloudNoise.glslh>

layout(set = 1, binding = 0, rgba16f) restrict writeonly uniform image3D o_NoiseVolume;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

void main()
{
	ivec3 size = imageSize(o_NoiseVolume);
	ivec3 coord = ivec3(gl_GlobalInvocationID);
	if (coord.x >= size.x || coord.y >= size.y || coord.z >= size.z)
		return;

	vec3 uvw = (vec3(coord) + vec3(0.5)) / vec3(size);
	vec3 curl = CloudCurlNoise(uvw, 4.0);

	imageStore(o_NoiseVolume, coord, vec4(curl, 0.0));
}
