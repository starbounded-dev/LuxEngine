// Bakes the volumetric-cloud detail volume (Nubis "detail noise").
//
// Output: 32^3 RGBA16F, tileable. RGB are Worley FBM at increasing frequency,
// used by the raymarcher to erode the cloud edges into wispy, high-frequency
// detail near the camera. A is unused (kept for alignment / future curl).

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

	float worley2 = CloudWorleyFBM(uvw, 2.0);
	float worley4 = CloudWorleyFBM(uvw, 4.0);
	float worley8 = CloudWorleyFBM(uvw, 8.0);

	imageStore(o_NoiseVolume, coord, vec4(worley2, worley4, worley8, 1.0));
}
