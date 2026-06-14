// Temporal anti-aliasing resolve. Reprojects the previous resolved frame using
// per-object motion vectors (velocity-dilated to the closest surface, falling back
// to camera reprojection for background), then variance-clips the history to the
// current neighbourhood in YCoCg space to suppress ghosting before blending.
// Output is copied back into scene color by the renderer.
#version 450 core
#pragma stage : comp

#include <Buffers.glslh>
#include <Samplers.glslh>

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 0) uniform texture2D u_SceneColor; // current frame (jittered)
layout(set = 1, binding = 1) uniform texture2D u_History;     // previous resolved frame
layout(set = 1, binding = 2) uniform texture2D u_Velocity;    // RG16F, current - previous UV
layout(set = 1, binding = 3) uniform texture2D u_Depth;       // reverse-z (1 = near, 0 = far)
layout(set = 1, binding = 4, rgba16f) uniform writeonly image2D o_Resolved;

layout(push_constant) uniform TAASettings
{
	float Blend;     // fraction of history retained
	uint HasHistory; // 0 on the first frame / after a cut
	uint Padding0;
	uint Padding1;
} u_Settings;

vec3 RGBToYCoCg(vec3 c)
{
	return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
	            0.5 * c.r - 0.5 * c.b,
	            -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c)
{
	float t = c.x - c.z;
	return vec3(t + c.y, c.x + c.z, t - c.y);
}

// Clip the history sample to the neighbourhood colour box (move it toward the box
// centre until it lies inside) — far better than a per-channel clamp for ghosting.
vec3 ClipToAABB(vec3 aabbMin, vec3 aabbMax, vec3 history)
{
	vec3 center = 0.5 * (aabbMax + aabbMin);
	vec3 extent = 0.5 * (aabbMax - aabbMin) + vec3(1e-4);
	vec3 dir = history - center;
	vec3 ts = extent / max(abs(dir), vec3(1e-4));
	float t = min(ts.x, min(ts.y, ts.z));
	return center + dir * min(t, 1.0);
}

// Camera-only reprojection from depth (background / pixels without a motion vector).
vec2 CameraReprojectUV(vec2 uv, float depth)
{
	vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec4 world = u_Camera.InverseViewProjectionMatrix * clip;
	world /= max(world.w, 1e-5);

	vec4 previousClip = u_Camera.PreviousViewProjectionMatrix * world;
	if (previousClip.w <= 1e-5)
		return vec2(-1.0);

	return previousClip.xy / previousClip.w * 0.5 + 0.5;
}

void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(o_Resolved);
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	vec2 texel = 1.0 / vec2(size);
	vec2 uv = (vec2(pixel) + 0.5) * texel;

	vec3 current = texture(sampler2D(u_SceneColor, r_PointSampler), uv).rgb;

	// 3x3 neighbourhood: YCoCg mean/variance for clipping, plus velocity dilation
	// (track the closest surface — reverse-z => the largest depth).
	vec3 m1 = vec3(0.0);
	vec3 m2 = vec3(0.0);
	float closestDepth = -1.0;
	vec2 closestOffset = vec2(0.0);
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec2 offset = vec2(x, y) * texel;
			vec3 c = RGBToYCoCg(texture(sampler2D(u_SceneColor, r_PointSampler), uv + offset).rgb);
			m1 += c;
			m2 += c * c;

			float d = texture(sampler2D(u_Depth, r_PointSampler), uv + offset).r;
			if (d > closestDepth)
			{
				closestDepth = d;
				closestOffset = offset;
			}
		}
	}
	m1 /= 9.0;
	m2 /= 9.0;
	vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
	const float gamma = 1.0; // tighter => less ghosting, more flicker
	vec3 aabbMin = m1 - gamma * sigma;
	vec3 aabbMax = m1 + gamma * sigma;

	// Reproject using the dilated per-object velocity; fall back to camera reprojection.
	vec2 velocity = texture(sampler2D(u_Velocity, r_PointSampler), uv + closestOffset).rg;
	vec2 historyUV = (dot(velocity, velocity) > 1e-12)
		? (uv - velocity)
		: CameraReprojectUV(uv, texture(sampler2D(u_Depth, r_PointSampler), uv).r);

	bool validHistory = u_Settings.HasHistory != 0u
		&& all(greaterThanEqual(historyUV, vec2(0.0)))
		&& all(lessThanEqual(historyUV, vec2(1.0)));

	vec3 resolved = current;
	if (validHistory)
	{
		vec3 history = RGBToYCoCg(texture(sampler2D(u_History, r_LinearSampler), historyUV).rgb);
		history = ClipToAABB(aabbMin, aabbMax, history);
		resolved = YCoCgToRGB(mix(RGBToYCoCg(current), history, clamp(u_Settings.Blend, 0.0, 0.97)));
	}

	imageStore(o_Resolved, pixel, vec4(resolved, 1.0));
}
