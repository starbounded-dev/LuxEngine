// ----------------------------------------------------------
// -- Lux ray-traced GI with ReSTIR GI reuse (RTGITrace + ) --
// ----------------------------------------------------------
//
// Same job and outputs as RTGITrace.glsl (a per-pixel estimate of the mean incident radiance, denoised afterwards by
// RTGIFilter.glsl), but each pixel resamples indirect paths (Ouyang et al. 2021) instead of using only its own:
//   - the pixel traces one cosine-weighted indirect path as before and keeps its first hit x2, the normal there and the
//     radiance leaving x2 as a reservoir sample;
//   - it merges the previous frame's reservoirs at the reprojected pixel and a few neighbours, re-evaluating each sample's
//     target at this surface (Lambert, cosine over pi) with the solid-angle Jacobian of moving the sample's origin;
//   - one visibility ray checks the winner (a blocked winner keeps weight 0, so it is not reused).
// The sample's radiance is treated as direction independent (diffuse x2), and the 1 / M normalisation is the paper's biased
// variant, so this is opt-in (SceneRendererOptions::RTGIReuse). See docs/GI_PLAN.md.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

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

layout(rgba16f, set = 1, binding = 90) uniform image2D o_RTGIAccum;
layout(rgba16f, set = 1, binding = 91) uniform writeonly image2D o_RTGIAlbedo;
layout(rgba16f, set = 1, binding = 92) uniform writeonly image2D o_RTGINormal;

layout(rgba32f, set = 1, binding = 110) uniform writeonly image2D o_RGSample;      // x2.xyz, M
layout(rgba32f, set = 1, binding = 111) uniform writeonly image2D o_RGSampleNormal; // n2.xyz, W
layout(rgba16f, set = 1, binding = 112) uniform writeonly image2D o_RGRadiance;     // radiance leaving x2, w = 1 for a sky sample
layout(rgba32f, set = 1, binding = 113) uniform writeonly image2D o_RGGPos;         // x1.xyz, w = 1 when a surface was hit
layout(rgba16f, set = 1, binding = 114) uniform writeonly image2D o_RGGNormal;      // n1.xyz
layout(set = 1, binding = 115) uniform texture2D u_RGPrevSample;
layout(set = 1, binding = 116) uniform texture2D u_RGPrevSampleNormal;
layout(set = 1, binding = 117) uniform texture2D u_RGPrevRadiance;
layout(set = 1, binding = 118) uniform texture2D u_RGPrevGPos;
layout(set = 1, binding = 119) uniform texture2D u_RGPrevGNormal;

const float MAX_HISTORY = 20.0;

float Lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	const uvec2 size = u_Push.SizeFrameBounces.xy;
	if (pixel.x >= size.x || pixel.y >= size.y)
		return;

	const uint frame = u_Push.SizeFrameBounces.z;
	g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 6151u + 11u)));
	const bool historyValid = u_Push.Counts.z != 0u;

	vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
	vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	vec3 dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);

	g_InitialConeAngle = 2.0 / (u_Camera.ProjectionMatrix[1][1] * float(size.y));
	Surface s;
	if (!TraceClosest(origin, dir, s))
	{
		imageStore(o_RTGIAccum, ivec2(pixel), vec4(0.0));
		imageStore(o_RTGIAlbedo, ivec2(pixel), vec4(0.0));
		imageStore(o_RTGINormal, ivec2(pixel), vec4(0.0));
		imageStore(o_RGSample, ivec2(pixel), vec4(0.0));
		imageStore(o_RGSampleNormal, ivec2(pixel), vec4(0.0));
		imageStore(o_RGRadiance, ivec2(pixel), vec4(0.0));
		imageStore(o_RGGPos, ivec2(pixel), vec4(0.0));
		imageStore(o_RGGNormal, ivec2(pixel), vec4(0.0));
		return;
	}
	const float viewDistance = length(s.P - origin);

	// This pixel's own path: cosine-weighted direction, first hit x2, radiance leaving x2 towards x1.
	vec3 t, b;
	Basis(s.N, t, b);
	float u1 = Rand(), u2 = Rand();
	float r = sqrt(u1);
	float phi = 2.0 * PI * u2;
	vec3 sampleDir = normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + s.N * sqrt(max(1.0 - u1, 0.0)));
	vec3 sampleOrigin = s.P + s.N * 2e-3;

	Surface s2;
	const bool hit2 = TraceClosest(sampleOrigin, sampleDir, s2);
	vec3 x2 = hit2 ? s2.P : sampleOrigin + sampleDir * 1.0e4;
	vec3 n2 = hit2 ? s2.N : -sampleDir;
	vec3 incident = TracePath(sampleOrigin, sampleDir, u_Push.SizeFrameBounces.w);
	float peak = max(incident.r, max(incident.g, incident.b));
	if (peak > 16.0)
		incident *= 16.0 / peak;

	// Reservoir: the target is the contribution (radiance * cos / pi) at this surface; cosine sampling makes the source
	// weight target / pdf = luminance(radiance).
	vec3 selectedPos = x2, selectedNormal = n2, selectedRadiance = incident;
	bool selectedSky = !hit2;
	float cosSelected = max(dot(s.N, sampleDir), 1e-4);
	float selectedTarget = Lum(incident) * cosSelected / PI;
	float weightSum = Lum(incident);
	float count = 1.0;
	bool selectedFresh = true; // the pixel's own path is visible by construction

	if (historyValid)
	{
		vec4 prevClip = u_Camera.PreviousViewProjectionMatrix * vec4(s.P, 1.0);
		vec2 prevNdc = prevClip.xy / prevClip.w;
		ivec2 prevPixel = ivec2(vec2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5) * vec2(size));
		for (uint tap = 0u; tap < 4u; tap++)
		{
			ivec2 q = prevPixel;
			if (tap > 0u)
				q += ivec2(round((vec2(Rand(), Rand()) * 2.0 - 1.0) * 8.0));
			if (q.x < 0 || q.y < 0 || q.x >= int(size.x) || q.y >= int(size.y))
				continue;
			const vec4 prevPos = texelFetch(u_RGPrevGPos, q, 0);
			if (prevPos.w < 0.5)
				continue;
			if (dot(texelFetch(u_RGPrevGNormal, q, 0).xyz, s.N) < 0.9 || distance(prevPos.xyz, s.P) > 0.05 * viewDistance + 0.02)
				continue;
			const vec4 sampleData = texelFetch(u_RGPrevSample, q, 0);
			const vec4 normalData = texelFetch(u_RGPrevSampleNormal, q, 0);
			const vec4 radianceData = texelFetch(u_RGPrevRadiance, q, 0);
			const float prevCount = min(sampleData.w, MAX_HISTORY);
			if (prevCount <= 0.0 || normalData.w <= 0.0)
				continue;

			const bool sky = radianceData.w > 0.5;
			vec3 toSample = sampleData.xyz - s.P;
			float distNew = length(toSample);
			vec3 dirNew = toSample / max(distNew, 1e-5);
			float cosNew = dot(s.N, dirNew);
			if (cosNew <= 0.0)
				continue;
			// Jacobian of the change of origin (x1' for x1), in solid angle at the sample's surface.
			float jacobian = 1.0;
			if (!sky)
			{
				vec3 toOld = sampleData.xyz - prevPos.xyz;
				float distOld = length(toOld);
				float cosOld = abs(dot(normalData.xyz, toOld / max(distOld, 1e-5)));
				float cosNew2 = abs(dot(normalData.xyz, dirNew));
				jacobian = (cosNew2 / max(distNew * distNew, 1e-6)) / max(cosOld / max(distOld * distOld, 1e-6), 1e-6);
				if (jacobian > 10.0 || jacobian < 0.1)
					continue;
			}
			const float target = Lum(radianceData.rgb) * cosNew / PI;
			const float weight = target * normalData.w * jacobian * prevCount;
			weightSum += weight;
			count += prevCount;
			if (Rand() * weightSum < weight)
			{
				selectedPos = sampleData.xyz;
				selectedNormal = normalData.xyz;
				selectedRadiance = radianceData.rgb;
				selectedSky = sky;
				cosSelected = cosNew;
				selectedTarget = target;
				selectedFresh = false;
			}
		}
	}

	float W = selectedTarget > 0.0 ? weightSum / (count * selectedTarget) : 0.0;
	if (!selectedFresh && W > 0.0)
	{
		vec3 toSample = selectedPos - s.P;
		float distance2 = length(toSample);
		if (Occluded(sampleOrigin, toSample / max(distance2, 1e-5), selectedSky ? 1.0e30 : distance2 - 4e-3))
			W = 0.0;
	}
	// Mean incident radiance: f * W with f = radiance * cos / pi (equals the radiance itself for the pixel's own sample).
	vec3 estimate = selectedRadiance * (cosSelected / PI) * W;

	// Running mean that settles to a 1/32 exponential average, restarting at frame 0 (as RTGITrace.glsl).
	vec3 history = imageLoad(o_RTGIAccum, ivec2(pixel)).rgb;
	float alpha = frame == 0u ? 1.0 : max(1.0 / float(frame + 1u), 1.0 / float(max(u_Push.Counts.w, 1u)));
	imageStore(o_RTGIAccum, ivec2(pixel), vec4(mix(history, estimate, alpha), 1.0));
	imageStore(o_RTGIAlbedo, ivec2(pixel), vec4(s.Albedo * (1.0 - s.Metal), viewDistance));
	imageStore(o_RTGINormal, ivec2(pixel), vec4(s.N, 0.0));

	imageStore(o_RGSample, ivec2(pixel), vec4(selectedPos, min(count, MAX_HISTORY)));
	imageStore(o_RGSampleNormal, ivec2(pixel), vec4(selectedNormal, W));
	imageStore(o_RGRadiance, ivec2(pixel), vec4(selectedRadiance, selectedSky ? 1.0 : 0.0));
	imageStore(o_RGGPos, ivec2(pixel), vec4(s.P, 1.0));
	imageStore(o_RGGNormal, ivec2(pixel), vec4(s.N, 0.0));
}
