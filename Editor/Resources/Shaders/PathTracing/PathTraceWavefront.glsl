// ----------------------------------------------------------
// -- Lux path tracer, ray-sorted (wavefront) variant        --
// ----------------------------------------------------------
//
// The same estimator as PathTrace.glsl, restructured so that rays leaving the first surface are traced in a coherent order: the
// first bounce of every path runs (mode 0), the surviving paths are suspended in a buffer and binned by the octahedral cell of
// their next ray direction (histogram, prefix scan, scatter: modes 0, 3, 4), then the rest of each path runs with neighbouring
// threads holding rays that point the same way (mode 5). Each path carries its own random state, so the image equals PathTrace's
// bit for bit; only the order of work changes. One shader, five modes (Counts.w), dispatched in sequence by PathTracePass.cpp.
// This is the software stand-in for hardware shader execution reordering, which needs a ray-tracing pipeline the inline
// ray-query tracer does not use; it is kept only if it measures faster (SceneRendererOptions::PathTracerRaySort).
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <PathTraceCommon.glslh>

// Identical to the Camera block in Buffers.glslh (same (set,binding), same name).
layout(std140, set = 2, binding = 0) uniform Camera
{
	mat4 ViewProjectionMatrix;
	mat4 InverseViewProjectionMatrix;
	mat4 ProjectionMatrix;
	mat4 InverseProjectionMatrix;
	mat4 ViewMatrix;
	mat4 InverseViewMatrix;
	vec2 NDCToViewMul;
	vec2 NDCToViewAdd;
	vec2 DepthUnpackConsts;
	vec2 CameraTanHalfFOV;
	mat4 UnjitteredViewProjectionMatrix;
	mat4 PreviousViewProjectionMatrix;
	vec2 Jitter;
	vec2 PreviousJitter;
	int IsOrthographic;
} u_Camera;

layout(rgba32f, set = 1, binding = 65) uniform image2D o_PathTraceAccum;
layout(rgba16f, set = 1, binding = 66) uniform writeonly image2D o_PathTraceImage;
layout(std430, set = 1, binding = 140) buffer PathStates { vec4 States[]; };       // 6 vec4 per path: origin+coneWidth, dir+coneAngle, throughput+prevPdf, radiance+rng bits, sigmaA+sigmaS.x, sigmaS.yz
layout(std430, set = 1, binding = 141) buffer PathKeys { uint Keys[]; };           // bin per pixel (64 = the path ended in the first bounce)
layout(std430, set = 1, binding = 142) buffer PathBins { uint Bins[]; };           // [0..64] counts, [65..129] offsets, [130..194] cursors, [195] live paths
layout(std430, set = 1, binding = 143) buffer PathSorted { uint Sorted[]; };       // pixel index per sorted slot

const uint BIN_COUNT = 64u;

uint OctahedralBin(vec3 d)
{
	d /= abs(d.x) + abs(d.y) + abs(d.z);
	vec2 e = d.z >= 0.0 ? d.xy : (1.0 - abs(d.yx)) * vec2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
	e = e * 0.5 + 0.5;
	return min(uint(e.x * 8.0), 7u) * 8u + min(uint(e.y * 8.0), 7u);
}

void StoreState(uint index, PathState st)
{
	States[6u * index + 0u] = vec4(st.Origin, st.ConeWidth);
	States[6u * index + 1u] = vec4(st.Dir, st.ConeAngle);
	States[6u * index + 2u] = vec4(st.Throughput, st.PrevPdf);
	States[6u * index + 3u] = vec4(st.Radiance, uintBitsToFloat(g_Rng));
	States[6u * index + 4u] = vec4(st.SigmaA, st.SigmaS.x);
	States[6u * index + 5u] = vec4(st.SigmaS.yz, st.InObject ? 1.0 : 0.0, 0.0);
}

PathState LoadState(uint index)
{
	const vec4 a = States[6u * index + 0u];
	const vec4 b = States[6u * index + 1u];
	const vec4 c = States[6u * index + 2u];
	const vec4 d = States[6u * index + 3u];
	const vec4 e = States[6u * index + 4u];
	const vec4 f = States[6u * index + 5u];
	PathState st;
	st.Origin = a.xyz;
	st.ConeWidth = a.w;
	st.Dir = b.xyz;
	st.ConeAngle = b.w;
	st.Throughput = c.xyz;
	st.PrevPdf = c.w;
	st.Radiance = d.xyz;
	st.SigmaA = e.xyz;
	st.SigmaS = vec3(e.w, f.xy);
	st.InObject = f.z > 0.5;
	g_Rng = floatBitsToUint(d.w);
	return st;
}

// Firefly clamp and the running mean, as PathTrace.glsl.
void Finalize(uvec2 pixel, uint frame, vec3 radiance)
{
	const float maxSample = 64.0;
	const float peak = max(radiance.r, max(radiance.g, radiance.b));
	if (peak > maxSample)
		radiance *= maxSample / peak;
	const vec3 previous = frame == 0u ? vec3(0.0) : imageLoad(o_PathTraceAccum, ivec2(pixel)).rgb;
	const vec3 mean = (previous * float(frame) + radiance) / float(frame + 1u);
	imageStore(o_PathTraceAccum, ivec2(pixel), vec4(mean, float(frame + 1u)));
	imageStore(o_PathTraceImage, ivec2(pixel), vec4(mean, 1.0));
}

void main()
{
	const uint width = u_Push.SizeFrameBounces.x;
	const uint height = u_Push.SizeFrameBounces.y;
	const uint frame = u_Push.SizeFrameBounces.z;
	const uint maxBounces = u_Push.SizeFrameBounces.w;
	const uint mode = u_Push.Counts.w;
	const uint rowStride = u_Push.Counts.z; // threads per dispatch row
	const uint linear = gl_GlobalInvocationID.y * rowStride + gl_GlobalInvocationID.x;
	const uvec2 pixel = gl_GlobalInvocationID.xy;

	if (mode == 1u) // clear the bins
	{
		if (linear < 196u)
			Bins[linear] = 0u;
		return;
	}
	if (mode == 3u) // exclusive prefix over the 64 bins
	{
		if (linear != 0u)
			return;
		uint running = 0u;
		for (uint k = 0u; k < BIN_COUNT; k++)
		{
			Bins[65u + k] = running;
			running += Bins[k];
		}
		Bins[195] = running;
		return;
	}
	if (mode == 5u) // the rest of every live path, in sorted order
	{
		if (linear >= Bins[195])
			return;
		const uint index = Sorted[linear];
		PathState st = LoadState(index);
		g_InitialConeAngle = 2.0 / (u_Camera.ProjectionMatrix[1][1] * float(height));
		TraceBounces(st, 1u, maxBounces, maxBounces);
		Finalize(uvec2(index % width, index / width), frame, st.Radiance);
		return;
	}

	if (pixel.x >= width || pixel.y >= height)
		return;
	const uint index = pixel.y * width + pixel.x;

	if (mode == 0u) // first bounce, then suspend
	{
		g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 9781u + 1u)));
		vec2 uv = (vec2(pixel) + vec2(Rand(), Rand())) / vec2(width, height);
		vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
		vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
		vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
		PathState st;
		st.Origin = u_Camera.InverseViewMatrix[3].xyz;
		st.Dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);
		st.Throughput = vec3(1.0);
		st.Radiance = vec3(0.0);
		st.PrevPdf = 0.0;
		st.ConeWidth = 0.0;
		g_InitialConeAngle = 2.0 / (u_Camera.ProjectionMatrix[1][1] * float(height));
		st.ConeAngle = g_InitialConeAngle;
		st.SigmaA = vec3(0.0);
		st.SigmaS = vec3(0.0);
		st.InObject = false;
		const bool alive = TraceBounces(st, 0u, 0u, maxBounces);
		StoreState(index, st);
		const uint key = alive ? OctahedralBin(st.Dir) : BIN_COUNT;
		Keys[index] = key;
		if (alive)
			atomicAdd(Bins[key], 1u);
		return;
	}

	// mode 4: scatter live paths into their bin; paths that already ended are finished here
	const uint key = Keys[index];
	if (key >= BIN_COUNT)
	{
		const PathState st = LoadState(index);
		Finalize(pixel, frame, st.Radiance);
		return;
	}
	const uint slot = Bins[65u + key] + atomicAdd(Bins[130u + key], 1u);
	Sorted[slot] = index;
}
