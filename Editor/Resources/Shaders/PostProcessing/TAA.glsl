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
	float Sharpness; // post-resolve unsharp strength (0 = off)
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

// 5-tap optimized Catmull-Rom history sampling. Plain bilinear reprojection
// re-blurs the accumulated image a little every frame, which is the dominant
// source of TAA softness; a bicubic (Catmull-Rom) fetch keeps the history sharp.
vec3 SampleHistoryCatmullRom(vec2 uv, vec2 texSize)
{
	vec2 samplePos = uv * texSize;
	vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
	vec2 f = samplePos - texPos1;

	vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	vec2 w3 = f * f * (-0.5 + 0.5 * f);

	vec2 w12 = w1 + w2;
	vec2 offset12 = w2 / w12;

	vec2 texPos0 = (texPos1 - 1.0) / texSize;
	vec2 texPos3 = (texPos1 + 2.0) / texSize;
	vec2 texPos12 = (texPos1 + offset12) / texSize;

	vec3 result = vec3(0.0);
	result += textureLod(sampler2D(u_History, r_LinearSampler), vec2(texPos12.x, texPos0.y), 0.0).rgb * w12.x * w0.y;
	result += textureLod(sampler2D(u_History, r_LinearSampler), vec2(texPos0.x, texPos12.y), 0.0).rgb * w0.x * w12.y;
	result += textureLod(sampler2D(u_History, r_LinearSampler), vec2(texPos12.x, texPos12.y), 0.0).rgb * w12.x * w12.y;
	result += textureLod(sampler2D(u_History, r_LinearSampler), vec2(texPos3.x, texPos12.y), 0.0).rgb * w3.x * w12.y;
	result += textureLod(sampler2D(u_History, r_LinearSampler), vec2(texPos12.x, texPos3.y), 0.0).rgb * w12.x * w3.y;

	return max(result, vec3(0.0));
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

	vec3 currentY = RGBToYCoCg(current);
	vec3 resolvedY = currentY;
	if (validHistory)
	{
		vec3 history = RGBToYCoCg(SampleHistoryCatmullRom(historyUV, vec2(size)));
		history = ClipToAABB(aabbMin, aabbMax, history);

		// Tonemap-weighted blend (Karis): weight each sample by 1/(1+luma) so bright
		// pixels don't dominate the history and cause flicker, without the blur of a
		// naive average. YCoCg .x is luma.
		float blend = clamp(u_Settings.Blend, 0.0, 0.97);
		float wCurrent = (1.0 - blend) / (1.0 + max(currentY.x, 0.0));
		float wHistory = blend / (1.0 + max(history.x, 0.0));
		resolvedY = (currentY * wCurrent + history * wHistory) / max(wCurrent + wHistory, 1e-5);

		// Unsharp the luma against the current-frame neighbourhood low-pass (m1) to
		// restore the micro-contrast that temporal accumulation softens.
		resolvedY.x += u_Settings.Sharpness * (resolvedY.x - m1.x);
	}

	vec3 resolved = max(YCoCgToRGB(resolvedY), vec3(0.0));
	imageStore(o_Resolved, pixel, vec4(resolved, 1.0));
}
