#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include <Buffers.glslh>
#include <PathTraceCommon.glslh>

// Three R32UI planes packed side by side. Each photon atomically adds its fixed-point RGB flux.
layout(r32ui, set = 1, binding = 164) uniform uimage2D o_CausticFlux;
layout(set = 1, binding = 166) uniform texture2D u_CausticDepth;

vec3 SampleUniformCone(vec3 axis, float cosineMinimum)
{
	const float cosine = mix(cosineMinimum, 1.0, Rand());
	const float sine = sqrt(max(1.0 - cosine * cosine, 0.0));
	const float phi = 2.0 * PI * Rand();
	vec3 tangent, bitangent;
	Basis(axis, tangent, bitangent);
	return normalize(axis * cosine + sine * (cos(phi) * tangent + sin(phi) * bitangent));
}

// A cone containing the glass bounds. Sampling the solid angle it covers keeps the same flux
// per steradian while concentrating rays near transmitters.
vec3 AimAtGlass(vec3 position, vec3 centre, float radius, out float solidAngle)
{
	const vec3 toCentre = centre - position;
	const float distanceSquared = dot(toCentre, toCentre);
	const bool outside = distanceSquared > radius * radius;
	const float minimum = outside ? sqrt(max(1.0 - radius * radius / distanceSquared, 0.0)) : -1.0;
	solidAngle = 2.0 * PI * (1.0 - minimum);
	return SampleUniformCone(outside ? toCentre * inversesqrt(distanceSquared) : vec3(0.0, 1.0, 0.0), minimum);
}

bool EmitPhoton(uint light, out vec3 origin, out vec3 direction, out vec3 flux, out float range)
{
	const vec4 a = Lights[4u * light];
	const vec4 b = Lights[4u * light + 1u];
	const vec4 c = Lights[4u * light + 2u];
	const vec4 d = Lights[4u * light + 3u];
	const int type = int(a.w + 0.5);
	const vec3 centre = u_Push.SkyRadiance.xyz;
	const float radius = u_Push.SkyRadiance.w;
	origin = a.xyz;
	range = 1.0e30;
	flux = b.rgb;

	if (type == 0)
	{
		direction = -normalize(a.xyz);
		vec3 x, y;
		Basis(direction, x, y);
		const float r = sqrt(Rand()) * radius;
		const float phi = 2.0 * PI * Rand();
		origin = centre - direction * u_Push.GroundRadiance.x + r * (x * cos(phi) + y * sin(phi));
		flux *= PI * radius * radius;
		return true;
	}

	float emitterArea = 1.0;
	vec3 emitterNormal = vec3(0.0);
	if (type == 3 || type == 4)
	{
		const vec3 plane = cross(c.xyz, d.xyz);
		const float planeArea = length(plane);
		if (planeArea <= 1e-8)
			return false;
		emitterNormal = -plane / planeArea;
		if (type == 3)
		{
			origin += (2.0 * Rand() - 1.0) * c.xyz + (2.0 * Rand() - 1.0) * d.xyz;
			emitterArea = 4.0 * planeArea;
		}
		else
		{
			const float r = sqrt(Rand());
			const float phi = 2.0 * PI * Rand();
			origin += r * (c.xyz * cos(phi) + d.xyz * sin(phi));
			emitterArea = PI * planeArea;
		}
	}
	else if (type == 5)
	{
		const float sourceRadius = length(c.xyz);
		emitterNormal = SampleUniformCone(vec3(0.0, 1.0, 0.0), -1.0);
		origin += sourceRadius * emitterNormal;
		emitterArea = 4.0 * PI * sourceRadius * sourceRadius;
	}
	else if (type == 6)
	{
		const float halfLength = length(c.xyz);
		const float sourceRadius = length(d.xyz);
		if (halfLength <= 1e-6)
			return false;
		vec3 x, y;
		Basis(c.xyz / halfLength, x, y);
		const float phi = 2.0 * PI * Rand();
		emitterNormal = x * cos(phi) + y * sin(phi);
		origin += (2.0 * Rand() - 1.0) * c.xyz + sourceRadius * emitterNormal;
		emitterArea = 4.0 * PI * halfLength * sourceRadius;
	}

	float solidAngle;
	direction = AimAtGlass(origin, centre, radius, solidAngle);
	flux *= solidAngle;
	if (type >= 3)
		flux *= emitterArea * max(dot(emitterNormal, direction), 0.0);
	else
	{
		range = b.w;
		if (type == 2)
			flux *= clamp((dot(direction, c.xyz) - c.w) / max(1.0 - c.w, 1e-3), 0.0, 1.0);
	}
	return max(flux.r, max(flux.g, flux.b)) > 0.0;
}

void DepositPhoton(vec3 position, vec3 flux, uint photonCount)
{
	const vec4 clip = u_Camera.ViewProjectionMatrix * vec4(position, 1.0);
	if (clip.w <= 0.0)
		return;
	const vec2 ndc = clip.xy / clip.w;
	const vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))))
		return;
	const ivec2 pixel = ivec2(uv * vec2(textureSize(u_CausticDepth, 0)));
	const float depth = texelFetch(u_CausticDepth, pixel, 0).r;
	const float viewDepth = -(u_Camera.ViewMatrix * vec4(position, 1.0)).z;
	if (depth <= 1e-6 || abs(LinearizeDeviceDepth(depth) - viewDepth) > 0.012 + 0.005 * viewDepth)
		return;

	const ivec2 storage = imageSize(o_CausticFlux);
	const ivec2 extent = ivec2(storage.x / 3, storage.y);
	const ivec2 splat = ivec2(uv * vec2(extent));
	// The cap reserves enough range even when all photons land within a single cell.
	const uint maximum = 0xffffffffu / photonCount;
	const uvec3 quantized = uvec3(min(round(max(flux, vec3(0.0)) * 64.0), vec3(float(maximum))));
	for (int channel = 0; channel < 3; channel++)
		if (quantized[channel] != 0u)
			imageAtomicAdd(o_CausticFlux, splat + ivec2(channel * extent.x, 0), quantized[channel]);
}

void main()
{
	const uint photon = gl_GlobalInvocationID.x;
	const uint photonCount = u_Push.SizeFrameBounces.w;
	if (photon >= photonCount || u_Push.Counts.x == 0u)
		return;
	g_Rng = PCG(photon + 317u);
	const uint light = ChooseLight(Rand());
	vec3 origin, direction, flux;
	float range;
	if (!EmitPhoton(light, origin, direction, flux, range))
		return;
	flux /= LightPmf(light);

	bool inGlass = false;
	bool refracted = false;
	vec3 absorption = vec3(0.0);
	float travelled = 0.0;
	for (uint crossing = 0u; crossing <= 4u; crossing++)
	{
		Surface hit;
		if (!TraceClosest(origin, direction, hit))
			return;
		const float segment = length(hit.P - origin);
		travelled += segment;
		if (travelled >= range)
			return;
		g_InObject = inGlass;
		flux *= inGlass ? exp(-absorption * segment) : FogTransmittance(origin, direction, segment);
		if (!hit.Glass)
		{
			if (crossing >= 2u && !inGlass && refracted)
			{
				const float fraction = travelled / range;
				const float window = max(1.0 - fraction * fraction * fraction * fraction, 0.0);
				DepositPhoton(hit.P, flux * window * window, photonCount);
			}
			return;
		}

		const float eta = hit.FrontFace ? 1.0 / hit.IOR : hit.IOR;
		const float cosine = clamp(dot(hit.N, -direction), 0.0, 1.0);
		const float f = (1.0 - hit.IOR) / (1.0 + hit.IOR);
		const float fresnel = f * f + (1.0 - f * f) * pow(1.0 - cosine, 5.0);
		const vec3 transmitted = refract(direction, hit.N, eta);
		if (dot(transmitted, transmitted) < 1e-6)
			return;
		flux *= hit.Transmission * (1.0 - fresnel);
		refracted = refracted || hit.IOR > 1.001;
		inGlass = hit.FrontFace;
		absorption = hit.GlassSigma;
		direction = transmitted;
		origin = hit.P + direction * 1e-3;
	}
}
