// Bakes the volumetric-cloud base-shape volume (Nubis "shape noise").
//
// Output: 128^3 RGBA16F, tileable.
//   R = low-frequency Perlin-Worley   -> the connected cloud masses
//   G = Worley FBM (freq 8)           -> first erosion band
//   B = Worley FBM (freq 16)          -> second erosion band
//   A = Worley FBM (freq 32)          -> third erosion band
// The raymarcher combines GBA into the R channel to round the bottoms and
// build cauliflower tops.

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

	float perlinWorley = CloudPerlinWorley(uvw, 4.0, 4.0);
	float worley8  = CloudWorleyFBM(uvw, 8.0);
	float worley16 = CloudWorleyFBM(uvw, 16.0);
	float worley32 = CloudWorleyFBM(uvw, 32.0);

	imageStore(o_NoiseVolume, coord, vec4(perlinWorley, worley8, worley16, worley32));
}
