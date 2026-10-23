// FFT convolution bloom: the engine's bloom (SceneRenderer::BloomCompute). One shader, one mode per dispatch:
//   BRIGHT    the scene, area-averaged and soft-thresholded, into the source region of the 512x512
//             transform grid, zeros elsewhere (the padding turns the FFT's circular convolution
//             into a linear one); R and G share u_GridA (two complex numbers), B is u_GridB
//   KERNEL    the lens glare point spread function, centred on texel (0, 0) with wrap-around
//   FFT       a radix-2 FFT of one grid row or column per workgroup, in shared memory
//   MULTIPLY  the spectrum times the kernel spectrum divided by its DC term, so the convolution
//             redistributes the bright pass's energy instead of adding to it
//   RESOLVE   the convolved source region, bilinearly upsampled into the bloom texture
// Everything is spatial and per frame: no history.
#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

#define FFT_SIZE 512u
#define MAX_STREAK_LINES 8 // BloomSettings::MaxStreakCount
#define FFT_LOG2_SIZE 9u
#define THREADS 256u

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0, rgba16f) restrict writeonly uniform image2D o_Bloom;
layout(binding = 1) uniform texture2D u_Input;
layout(binding = 2, rgba32f) restrict uniform image2D u_GridA;   // xy = R, zw = G (complex)
layout(binding = 3, rg32f) restrict uniform image2D u_GridB;     // xy = B (complex)
layout(binding = 4, rgba32f) restrict readonly uniform image2D u_KernelA;
layout(binding = 5, rg32f) restrict readonly uniform image2D u_KernelB;

layout(push_constant) uniform Uniforms
{
	vec4 Region;     // xy = source region in grid texels, zw = 1 / that
	vec4 InputSize;  // xy = scene colour size, zw = 1 / that
	vec4 OutputSize; // xy = bloom texture size, zw = 1 / that
	vec4 Threshold;  // x threshold, y threshold - knee, z knee * 2, w 0.25 / knee
	vec4 KernelShape; // x streak lines, y streak length, z streak intensity, w halo intensity
	vec4 KernelColor; // x chromatic spread, y streak rotation (radians)
	int Mode;
	int Axis;        // FFT: 0 rows, 1 columns
	int Inverse;     // FFT: 1 for the inverse transform
	int Unused;
} u_Uniforms;

#define MODE_BRIGHT   0
#define MODE_KERNEL   1
#define MODE_FFT      2
#define MODE_MULTIPLY 3
#define MODE_RESOLVE  4

const float PI = 3.14159265359;

shared vec2 s_R[FFT_SIZE];
shared vec2 s_G[FFT_SIZE];
shared vec2 s_B[FFT_SIZE];

vec2 ComplexMultiply(vec2 a, vec2 b)
{
	return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Linear index over a width x height image for the 1D-dispatched 2D modes.
bool PixelFromIndex(uint width, uint height, out ivec2 pixel)
{
	uint index = gl_GlobalInvocationID.x;
	pixel = ivec2(index % width, index / width);
	return index < width * height;
}

vec3 SoftThreshold(vec3 color)
{
	float brightness = max(color.r, max(color.g, color.b));
	float soft = clamp(brightness - u_Uniforms.Threshold.y, 0.0, u_Uniforms.Threshold.z);
	soft = u_Uniforms.Threshold.w * soft * soft;
	return color * (max(soft, brightness - u_Uniforms.Threshold.x) / max(brightness, 1e-4));
}

void Bright()
{
	ivec2 pixel;
	if (!PixelFromIndex(FFT_SIZE, FFT_SIZE, pixel))
		return;

	vec3 color = vec3(0.0);
	if (pixel.x < int(u_Uniforms.Region.x) && pixel.y < int(u_Uniforms.Region.y))
	{
		// Area-average the scene over this grid texel's footprint with 4x4 bilinear taps, so a small
		// bright spot keeps its energy whatever its sub-texel position.
		vec2 uv = (vec2(pixel) + 0.5) * u_Uniforms.Region.zw;
		vec2 footprint = u_Uniforms.Region.zw;
		for (int y = 0; y < 4; y++)
		{
			for (int x = 0; x < 4; x++)
			{
				vec2 offset = (vec2(x, y) + 0.5) * 0.25 - 0.5;
				color += textureLod(sampler2D(u_Input, r_LinearSampler), uv + offset * footprint, 0.0).rgb;
			}
		}
		color = SoftThreshold(max(color / 16.0, vec3(0.0)));
	}
	imageStore(u_GridA, pixel, vec4(color.r, 0.0, color.g, 0.0));
	imageStore(u_GridB, pixel, vec4(color.b, 0.0, 0.0, 0.0));
}

// Lens glare: a sharp core, a power-law halo and thin streak lines through the centre, the red
// channel wider than the blue by the chromatic spread; it fades out well inside half the grid so
// the convolution never wraps.
float GlarePSF(vec2 offset, float scale)
{
	vec2 d = offset / scale;
	float radius = length(d);
	float core = exp(-radius * radius * 0.5);
	float halo = 0.03 * u_Uniforms.KernelShape.w / pow(1.0 + radius * radius * 0.25, 1.5);
	float streaks = 0.0;
	int lines = int(u_Uniforms.KernelShape.x);
	float streakLength = max(u_Uniforms.KernelShape.y, 1e-3) * 14.0;
	for (int i = 0; i < MAX_STREAK_LINES; i++)
	{
		if (i >= lines)
			break;
		float angle = PI * float(i) / float(lines) + u_Uniforms.KernelColor.y;
		vec2 direction = vec2(cos(angle), sin(angle));
		float along = abs(dot(d, direction));
		float across = abs(dot(d, vec2(-direction.y, direction.x)));
		streaks += exp(-across * across * 2.0) * exp(-along / streakLength);
	}
	float window = 1.0 - smoothstep(150.0, 230.0, length(offset));
	return (core + halo + 0.0015 * u_Uniforms.KernelShape.z * streaks) * window;
}

void Kernel()
{
	ivec2 pixel;
	if (!PixelFromIndex(FFT_SIZE, FFT_SIZE, pixel))
		return;

	// Centred on (0, 0): offsets past half the grid wrap to negative.
	vec2 offset = vec2(pixel);
	offset = mix(offset, offset - float(FFT_SIZE), greaterThanEqual(offset, vec2(FFT_SIZE / 2u)));
	float spread = u_Uniforms.KernelColor.x;
	imageStore(u_GridA, pixel, vec4(GlarePSF(offset, 1.0 + spread), 0.0, GlarePSF(offset, 1.0), 0.0));
	imageStore(u_GridB, pixel, vec4(GlarePSF(offset, 1.0 - spread), 0.0, 0.0, 0.0));
}

void FFT()
{
	uint line = gl_WorkGroupID.x;
	uint thread = gl_LocalInvocationID.x;
	bool columns = u_Uniforms.Axis != 0;

	// Load in bit-reversed order (decimation in time).
	for (uint k = 0u; k < FFT_SIZE / THREADS; k++)
	{
		uint i = thread + k * THREADS;
		uint reversed = bitfieldReverse(i) >> (32u - FFT_LOG2_SIZE);
		ivec2 coord = columns ? ivec2(line, i) : ivec2(i, line);
		vec4 a = imageLoad(u_GridA, coord);
		s_R[reversed] = a.xy;
		s_G[reversed] = a.zw;
		s_B[reversed] = imageLoad(u_GridB, coord).xy;
	}
	barrier();

	float direction = u_Uniforms.Inverse != 0 ? 1.0 : -1.0;
	for (uint size = 2u; size <= FFT_SIZE; size <<= 1u)
	{
		uint halfSize = size >> 1u;
		uint position = thread % halfSize;
		uint i0 = (thread / halfSize) * size + position;
		uint i1 = i0 + halfSize;
		float angle = direction * 2.0 * PI * float(position) / float(size);
		vec2 twiddle = vec2(cos(angle), sin(angle));

		vec2 r = ComplexMultiply(s_R[i1], twiddle);
		vec2 g = ComplexMultiply(s_G[i1], twiddle);
		vec2 b = ComplexMultiply(s_B[i1], twiddle);
		vec2 r0 = s_R[i0];
		vec2 g0 = s_G[i0];
		vec2 b0 = s_B[i0];
		s_R[i0] = r0 + r; s_R[i1] = r0 - r;
		s_G[i0] = g0 + g; s_G[i1] = g0 - g;
		s_B[i0] = b0 + b; s_B[i1] = b0 - b;
		barrier();
	}

	float scale = u_Uniforms.Inverse != 0 ? 1.0 / float(FFT_SIZE) : 1.0;
	for (uint k = 0u; k < FFT_SIZE / THREADS; k++)
	{
		uint i = thread + k * THREADS;
		ivec2 coord = columns ? ivec2(line, i) : ivec2(i, line);
		imageStore(u_GridA, coord, vec4(s_R[i], s_G[i]) * scale);
		imageStore(u_GridB, coord, vec4(s_B[i] * scale, 0.0, 0.0));
	}
}

void Multiply()
{
	ivec2 pixel;
	if (!PixelFromIndex(FFT_SIZE, FFT_SIZE, pixel))
		return;

	// The DC term is the PSF's sum: dividing by it normalises the kernel per channel.
	vec4 dcA = imageLoad(u_KernelA, ivec2(0));
	vec2 dcB = imageLoad(u_KernelB, ivec2(0)).xy;
	vec4 kernelA = imageLoad(u_KernelA, pixel);
	vec2 kernelB = imageLoad(u_KernelB, pixel).xy;
	vec4 a = imageLoad(u_GridA, pixel);
	vec2 b = imageLoad(u_GridB, pixel).xy;
	vec2 r = ComplexMultiply(a.xy, kernelA.xy) / max(dcA.x, 1e-8);
	vec2 g = ComplexMultiply(a.zw, kernelA.zw) / max(dcA.z, 1e-8);
	vec2 bb = ComplexMultiply(b, kernelB) / max(dcB.x, 1e-8);
	imageStore(u_GridA, pixel, vec4(r, g));
	imageStore(u_GridB, pixel, vec4(bb, 0.0, 0.0));
}

vec3 GridColor(ivec2 coord)
{
	coord = clamp(coord, ivec2(0), ivec2(u_Uniforms.Region.xy) - 1);
	vec4 a = imageLoad(u_GridA, coord);
	return vec3(a.x, a.z, imageLoad(u_GridB, coord).x);
}

void Resolve()
{
	ivec2 pixel;
	if (!PixelFromIndex(uint(u_Uniforms.OutputSize.x), uint(u_Uniforms.OutputSize.y), pixel))
		return;

	vec2 position = (vec2(pixel) + 0.5) * u_Uniforms.OutputSize.zw * u_Uniforms.Region.xy - 0.5;
	ivec2 base = ivec2(floor(position));
	vec2 f = position - vec2(base);
	vec3 color = mix(
		mix(GridColor(base), GridColor(base + ivec2(1, 0)), f.x),
		mix(GridColor(base + ivec2(0, 1)), GridColor(base + ivec2(1, 1)), f.x), f.y);
	imageStore(o_Bloom, pixel, vec4(max(color, vec3(0.0)), 1.0));
}

void main()
{
	switch (u_Uniforms.Mode)
	{
		case MODE_BRIGHT:   Bright();   break;
		case MODE_KERNEL:   Kernel();   break;
		case MODE_FFT:      FFT();      break;
		case MODE_MULTIPLY: Multiply(); break;
		case MODE_RESOLVE:  Resolve();  break;
	}
}
