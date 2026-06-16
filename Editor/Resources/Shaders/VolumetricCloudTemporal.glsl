// Temporal scattering integration for the volumetric clouds (Frostbite §5.5.3).
//
// The cloud raymarch is jittered per-frame and uses few samples; this pass
// reprojects the previous resolved cloud buffer using the cloud front distance
// and the previous view-projection, then blends it with the current frame via an
// exponential moving average (with a neighbourhood clamp to suppress ghosting).
// Runs at cloud (half) resolution, before the upsample composite.

#version 450 core
#pragma stage : comp

#include <Buffers.glslh>
#include <Samplers.glslh>

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 1, binding = 0) uniform texture2D u_CurrentCloud;      // raw raymarch: rgb colour, a transmittance-alpha
layout(set = 1, binding = 1) uniform texture2D u_CurrentCloudDepth; // x = cloud front distance
layout(set = 1, binding = 2) uniform texture2D u_HistoryCloud;      // previous resolved cloud
layout(set = 1, binding = 3, rgba16f) uniform writeonly image2D o_ResolvedCloud;

layout(push_constant) uniform Settings
{
	float Blend;     // fraction of history retained (EMA)
	uint  HasHistory;
	vec2  Padding;
} u_Settings;

const float LUX_CLOUD_MAX_DEPTH = 65000.0;

void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(o_ResolvedCloud);
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	vec2 texel = 1.0 / vec2(size);
	vec2 uv = (vec2(pixel) + 0.5) * texel;

	vec4 current = texture(sampler2D(u_CurrentCloud, r_PointSampler), uv);

	// Reconstruct the cloud world position from the front distance, then project
	// it with the previous frame's view-projection to find the history texel.
	vec2 clip = uv * 2.0 - 1.0;
	vec3 cameraPosition = u_Scene.CameraPosition;
	vec4 farWorld = u_Camera.InverseViewProjectionMatrix * vec4(clip, 0.0, 1.0);
	farWorld /= max(farWorld.w, 1.0e-6);
	vec3 viewDirection = normalize(farWorld.xyz - cameraPosition);

	float frontDistance = min(texture(sampler2D(u_CurrentCloudDepth, r_PointSampler), uv).x, LUX_CLOUD_MAX_DEPTH);
	vec3 cloudWorld = cameraPosition + viewDirection * frontDistance;

	vec4 previousClip = u_Camera.PreviousViewProjectionMatrix * vec4(cloudWorld, 1.0);
	vec2 historyUV = previousClip.xy / max(previousClip.w, 1.0e-6) * 0.5 + 0.5;

	bool validHistory = u_Settings.HasHistory != 0u
		&& previousClip.w > 1.0e-6
		&& all(greaterThanEqual(historyUV, vec2(0.0)))
		&& all(lessThanEqual(historyUV, vec2(1.0)));

	vec4 resolved = current;
	if (validHistory)
	{
		vec4 history = texture(sampler2D(u_HistoryCloud, r_LinearSampler), historyUV);

		// Clamp the history to the current 3x3 neighbourhood so fast camera motion
		// or evolving clouds don't leave trails.
		vec4 neighbourMin = current;
		vec4 neighbourMax = current;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				vec4 c = texture(sampler2D(u_CurrentCloud, r_PointSampler), uv + vec2(float(x), float(y)) * texel);
				neighbourMin = min(neighbourMin, c);
				neighbourMax = max(neighbourMax, c);
			}
		}
		vec4 extent = (neighbourMax - neighbourMin) * 0.25 + vec4(1.0e-3);
		history = clamp(history, neighbourMin - extent, neighbourMax + extent);

		resolved = mix(current, history, clamp(u_Settings.Blend, 0.0, 0.97));
	}

	imageStore(o_ResolvedCloud, pixel, resolved);
}
