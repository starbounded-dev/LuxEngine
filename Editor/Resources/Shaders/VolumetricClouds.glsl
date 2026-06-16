#version 450 core
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec2 v_ClipPosition;

void main()
{
	v_TexCoord = a_TexCoord;
	v_ClipPosition = a_Position.xy;
	gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}

#version 450 core
#pragma stage : frag

// ============================================================================
// Procedural volumetric cloudscape (RDR2 / Nubis style).
//
// Renders up to three atmospheric tiers (low / mid / high) of real cloud types
// in a single raymarch, using the baked 3D noise volumes:
//   u_CloudBaseShape : 128^3 Perlin-Worley + Worley FBM (connected masses)
//   u_CloudDetail    : 32^3  Worley FBM            (edge erosion / wisps)
// Lighting is energy-conserving (Beer-Powder + dual-lobe HG phase + a
// Wrenninge-style multiple-scattering octave approximation + sky ambient).
// ============================================================================

#include <Samplers.glslh>
#include <Atmosphere.glslh>

layout(set = 1, binding = 1) uniform texture2D u_DepthTexture;
layout(set = 1, binding = 2) uniform texture3D u_CloudBaseShape;
layout(set = 1, binding = 3) uniform texture3D u_CloudDetail;
layout(set = 1, binding = 4) uniform texture3D u_CloudCurl;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;
layout(location = 1) out vec4 o_Depth;

const float LUX_CLOUD_MAX_DEPTH = 65000.0;
const float LUX_CLOUD_MIN_DENSITY = 0.0005;

// World-space tiling distances for the noise volumes.
const float BASE_SHAPE_TILE_METERS = 9000.0;
const float DETAIL_TILE_METERS     = 450.0;
const float CURL_TILE_METERS       = 4000.0; // curl-noise repeat distance
const float CURL_STRENGTH_METERS   = 250.0;  // max detail advection near the cloud base

// --------------------------------------------------------------------------
// UBO accessors
// --------------------------------------------------------------------------
float GetGlobalCoverage() { return clamp(u_Atmosphere.CloudGlobal0.x, 0.0, 1.0); }
float GetGlobalDensity()  { return max(u_Atmosphere.CloudGlobal0.y, 0.0); }
vec2  GetWindDirection()
{
	vec2 d = u_Atmosphere.CloudGlobal0.zw;
	return dot(d, d) < 0.0001 ? vec2(1.0, 0.0) : normalize(d);
}
float GetWindSpeed()    { return u_Atmosphere.CloudGlobal1.x; }
float GetCloudTime()    { return u_Atmosphere.CloudGlobal1.y; }
float GetWeatherScale() { return max(u_Atmosphere.CloudGlobal1.z, 1.0e-7); }

float GetCloudMaxTraceDistance() { return max(u_Atmosphere.CloudRender0.x, 1.0); }

// --------------------------------------------------------------------------
// Math helpers
// --------------------------------------------------------------------------
float Remap(float v, float oldMin, float oldMax, float newMin, float newMax)
{
	return newMin + (clamp(v, min(oldMin, oldMax), max(oldMin, oldMax)) - oldMin) / max(oldMax - oldMin, 1.0e-5) * (newMax - newMin);
}

float PhaseHG(float cosTheta, float g)
{
	g = clamp(g, -0.95, 0.95);
	float g2 = g * g;
	float denom = max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5), 1.0e-4);
	return (1.0 - g2) / (4.0 * PI * denom);
}

// Dual-lobe HG (forward + back) with a modest silver-lining boost near the sun.
// NOTE: PhaseHG is already normalised (includes the 1/4pi term); it is used
// directly as the RTE scattering phase (do NOT re-multiply by 4pi or the
// forward lobe blows highlights out to pure white).
float CloudPhase(float cosTheta)
{
	float g0 = u_Atmosphere.CloudLighting2.x;
	float g1 = u_Atmosphere.CloudLighting2.y;
	float blend = clamp(u_Atmosphere.CloudLighting2.z, 0.0, 1.0);
	float dual = mix(PhaseHG(cosTheta, g0), PhaseHG(cosTheta, g1), blend);
	float silver = pow(max(cosTheta, 0.0), 8.0) * u_Atmosphere.CloudLighting1.z * 0.1;
	return dual + silver;
}

bool IntersectSphere(vec3 origin, vec3 direction, vec3 center, float radius, out vec2 result)
{
	vec3 localOrigin = origin - center;
	float b = dot(localOrigin, direction);
	float c = dot(localOrigin, localOrigin) - radius * radius;
	float discriminant = b * b - c;
	if (discriminant < 0.0)
		return false;
	float root = sqrt(discriminant);
	result = vec2(-b - root, -b + root);
	return result.y > 0.0;
}

// --------------------------------------------------------------------------
// Cloud-type density-height gradient.
//
// A trapezoidal profile morphed by the per-layer cloud "type" [0,1]:
//   0.0 -> stratus        (low flat slab)
//   0.5 -> stratocumulus  (rolling)
//   1.0 -> cumulus        (rounded bottom, billowy top)
// Cumulonimbus is produced by adding an anvil widening on top.
// --------------------------------------------------------------------------
float Trapezoid(float h, float a, float b, float c, float d)
{
	return smoothstep(a, b, h) * (1.0 - smoothstep(c, d, h));
}

float DensityHeightGradient(float h, float type, float anvil)
{
	float stratus       = Trapezoid(h, 0.0, 0.07, 0.18, 0.35);
	float stratocumulus = Trapezoid(h, 0.0, 0.15, 0.45, 0.65);
	float cumulus       = Trapezoid(h, 0.0, 0.28, 0.55, 0.95);

	float t = clamp(type, 0.0, 1.0);
	float gradient = t < 0.5
		? mix(stratus, stratocumulus, t * 2.0)
		: mix(stratocumulus, cumulus, (t - 0.5) * 2.0);

	if (anvil > 0.001)
	{
		// Cumulonimbus: spread density toward the top into an anvil.
		float anvilProfile = Trapezoid(h, 0.0, 0.1, 0.85, 1.0);
		gradient = mix(gradient, max(gradient, anvilProfile), clamp(anvil, 0.0, 1.0));
	}
	return gradient;
}

// Large-scale procedural weather (coverage) FBM driven over the XZ plane.
float WeatherCoverage(vec2 worldXZ, float coverageBias)
{
	float value = 0.0;
	float amplitude = 0.55;
	vec2 p = worldXZ;
	for (uint i = 0u; i < 4u; i++)
	{
		value += ValueNoise3D(vec3(p, 19.7)) * amplitude;
		p = p * 2.13 + vec2(31.7, 17.3);
		amplitude *= 0.5;
	}
	return clamp(value + coverageBias, 0.0, 1.0);
}

// --------------------------------------------------------------------------
// Evaluate the density of a single tier at a world position.
// 'cheap' skips the detail erosion (used by the light cone march / far LOD).
// --------------------------------------------------------------------------
float SampleLayerDensity(int layerIndex, vec3 worldPosition, float altitude, float lod, bool cheap)
{
	vec4 p0 = u_Atmosphere.CloudLayers[layerIndex].Params0;
	vec4 p1 = u_Atmosphere.CloudLayers[layerIndex].Params1;
	vec4 p2 = u_Atmosphere.CloudLayers[layerIndex].Params2;
	vec4 p3 = u_Atmosphere.CloudLayers[layerIndex].Params3;

	if (p3.y < 0.5) // disabled
		return 0.0;

	float bottom = p0.x;
	float thickness = max(p0.y, 1.0);
	float h = (altitude - bottom) / thickness;
	if (h < 0.0 || h > 1.0)
		return 0.0;

	// Height skew biases detail toward the cloud base (wispy bottoms) or top.
	float gradient = DensityHeightGradient(h, p0.w, p2.z);
	if (gradient <= 0.0001)
		return 0.0;

	// Wind advection: rotate the global wind by the per-layer angle offset.
	vec2 windDir = GetWindDirection();
	float ca = cos(p2.y);
	float sa = sin(p2.y);
	vec2 layerWind = vec2(windDir.x * ca - windDir.y * sa, windDir.x * sa + windDir.y * ca);
	float windAmount = GetWindSpeed() * p2.x * GetCloudTime();
	vec3 windOffset = vec3(layerWind.x, 0.0, layerWind.y) * windAmount;
	vec3 animPos = worldPosition + windOffset;

	// Large-scale coverage from procedural weather. Bias toward fuller skies so
	// the default look reads as a cloudscape rather than sparse wisps.
	float weather = WeatherCoverage((animPos.xz) * GetWeatherScale(), p3.z);
	float coverage = clamp(weather * p0.z * GetGlobalCoverage() * 2.6, 0.0, 1.0);
	if (coverage <= 0.001)
		return 0.0;

	// Base shape volume (low-frequency connected masses).
	vec3 baseCoord = animPos / (BASE_SHAPE_TILE_METERS / max(p1.y, 1.0e-3));
	vec4 baseNoise = texture(sampler3D(u_CloudBaseShape, r_RepeatSampler), baseCoord);
	float lowFreqFBM = baseNoise.g * 0.625 + baseNoise.b * 0.25 + baseNoise.a * 0.125;
	float base = Remap(baseNoise.r, lowFreqFBM - 1.0, 1.0, 0.0, 1.0);

	// Carve the raw shape by the coverage field (Nubis), THEN shape it
	// vertically by the height gradient. Carving before the gradient keeps the
	// cloud bodies full instead of eroding them to almost nothing.
	float density = Remap(base, 1.0 - coverage, 1.0, 0.0, 1.0) * gradient;
	if (density <= 0.0005)
		return 0.0;

	// Detail erosion (gentle): erode edges into wisps without gutting the body.
	float detailStrength = clamp(p1.w, 0.0, 1.0) * (1.0 - smoothstep(0.4, 0.95, lod));
	if (!cheap && detailStrength > 0.001)
	{
		// Curl-noise advection: swirl the detail erosion, strongest near the cloud
		// base, into wispy twisting tendrils (Nubis / Decima look).
		vec3 curl = texture(sampler3D(u_CloudCurl, r_RepeatSampler), animPos / CURL_TILE_METERS).xyz;
		float wisp = 1.0 - clamp(h, 0.0, 1.0);
		vec3 detailWorld = animPos + windOffset * 0.5 + vec3(0.0, GetCloudTime() * 8.0, 0.0) + curl * (CURL_STRENGTH_METERS * wisp);
		vec3 detailCoord = detailWorld / (DETAIL_TILE_METERS / max(p1.z, 1.0e-3));
		vec3 detailNoise = texture(sampler3D(u_CloudDetail, r_RepeatSampler), detailCoord).rgb;
		float detailFBM = detailNoise.r * 0.625 + detailNoise.g * 0.25 + detailNoise.b * 0.125;
		float erodeShape = mix(detailFBM, 1.0 - detailFBM, clamp(h * 1.5, 0.0, 1.0));
		density = Remap(density, erodeShape * detailStrength * 0.5, 1.0, 0.0, 1.0);
	}

	return clamp(density, 0.0, 1.0) * max(p1.x, 0.0);
}

// Sum the density contribution of every enabled tier at a world position.
float SampleCloudDensity(vec3 worldPosition, float lod, bool cheap)
{
	float altitude = GetAtmosphereAltitude(worldPosition);
	float density = 0.0;
	density += SampleLayerDensity(0, worldPosition, altitude, lod, cheap);
	density += SampleLayerDensity(1, worldPosition, altitude, lod, cheap);
	density += SampleLayerDensity(2, worldPosition, altitude, lod, cheap);
	return clamp(density * GetGlobalDensity(), 0.0, 2.0);
}

// --------------------------------------------------------------------------
// Combined cloud shell: intersect the band spanning all enabled tiers.
// --------------------------------------------------------------------------
bool GetCloudShellRadii(out float bottomRadius, out float topRadius)
{
	float planetRadius = GetAtmospherePlanetRadiusMeters();
	float minBottom = LUX_CLOUD_MAX_DEPTH;
	float maxTop = 0.0;
	bool any = false;
	for (int i = 0; i < 3; i++)
	{
		vec4 p0 = u_Atmosphere.CloudLayers[i].Params0;
		vec4 p3 = u_Atmosphere.CloudLayers[i].Params3;
		if (p3.y < 0.5)
			continue;
		any = true;
		minBottom = min(minBottom, p0.x);
		maxTop = max(maxTop, p0.x + max(p0.y, 1.0));
	}
	bottomRadius = planetRadius + minBottom;
	topRadius = planetRadius + maxTop;
	return any && topRadius > bottomRadius;
}

bool IntersectCloudShell(vec3 origin, vec3 direction, out float t0, out float t1)
{
	float bottomRadius, topRadius;
	if (!GetCloudShellRadii(bottomRadius, topRadius))
		return false;

	vec3 center = GetAtmospherePlanetCenter();
	vec2 topHit;
	if (!IntersectSphere(origin, direction, center, topRadius, topHit))
		return false;

	vec2 bottomHit;
	bool hitBottom = IntersectSphere(origin, direction, center, bottomRadius, bottomHit);

	float originRadius = length(origin - center);
	if (originRadius < bottomRadius)
	{
		// Below the deck looking up: from the bottom-sphere exit to the top exit.
		t0 = max(bottomHit.y, 0.0);
		t1 = topHit.y;
	}
	else if (originRadius > topRadius)
	{
		// Above the deck looking down: from top entry to bottom entry (or top exit).
		t0 = max(topHit.x, 0.0);
		t1 = hitBottom && bottomHit.x > 0.0 ? bottomHit.x : topHit.y;
	}
	else
	{
		// Inside the deck.
		t0 = 0.0;
		t1 = hitBottom && bottomHit.x > 0.0 ? bottomHit.x : topHit.y;
	}
	return t1 > t0;
}

// --------------------------------------------------------------------------
// Light energy reaching a sample from the sun (cone march + multi-scatter).
// --------------------------------------------------------------------------
float SampleSunOpticalDepth(vec3 worldPosition, vec3 sunDirection)
{
	uint coneSteps = uint(clamp(u_Atmosphere.CloudRender1.w, 1.0, 8.0));
	float stepLength = 22.0;
	float opticalDepth = 0.0;
	// Cone-shaped offsets so the shadow is soft rather than banded.
	const vec3 kConeOffsets[6] = vec3[6](
		vec3(0.1, 0.25, -0.15), vec3(-0.2, 0.1, 0.2), vec3(0.15, -0.2, 0.1),
		vec3(-0.1, 0.3, -0.25), vec3(0.25, 0.15, 0.2), vec3(0.0, 0.0, 0.0));

	for (uint i = 0u; i < 6u; i++)
	{
		if (i >= coneSteps)
			break;
		float t = stepLength * (float(i) + 1.0);
		float coneRadius = t * 0.12;
		vec3 samplePos = worldPosition + sunDirection * t + kConeOffsets[i] * coneRadius;
		opticalDepth += SampleCloudDensity(samplePos, 0.7, true) * stepLength;
		stepLength *= 1.6; // widen with distance (cone)
	}
	return opticalDepth;
}

void StoreEmptyCloud(float sceneDistance)
{
	o_Color = vec4(0.0);
	o_Depth = vec4(LUX_CLOUD_MAX_DEPTH, min(sceneDistance, LUX_CLOUD_MAX_DEPTH), LUX_CLOUD_MAX_DEPTH, LUX_CLOUD_MAX_DEPTH);
}

float GetCloudLODFactor(float distanceToCamera)
{
	float maxTrace = GetCloudMaxTraceDistance();
	float lodStart = clamp(u_Atmosphere.CloudRender0.z, 0.0, maxTrace);
	return smoothstep(lodStart, maxTrace, clamp(distanceToCamera, 0.0, maxTrace));
}

float GetCloudDistanceFade(float t0)
{
	float maxTrace = GetCloudMaxTraceDistance();
	float fade = clamp(u_Atmosphere.CloudRender0.y, 0.0, maxTrace);
	if (fade <= 0.0001)
		return t0 < maxTrace ? 1.0 : 0.0;
	float fadeStart = max(maxTrace - fade, 0.0);
	return 1.0 - smoothstep(fadeStart, maxTrace, clamp(t0, 0.0, maxTrace));
}

void main()
{
	float depth = texture(sampler2D(u_DepthTexture, r_PointSampler), v_TexCoord).r;
	vec3 cameraPosition = GetAtmosphereCameraPosition();
	vec3 viewDirection = GetAtmosphereViewDirection(v_ClipPosition);

	float sceneDistance = LUX_CLOUD_MAX_DEPTH;
	if (depth > 0.000001)
	{
		vec3 scenePosition = ReconstructAtmosphereWorldPosition(v_ClipPosition, depth);
		sceneDistance = min(length(scenePosition - cameraPosition), LUX_CLOUD_MAX_DEPTH);
	}

	if (u_Atmosphere.Flags.y == 0u)
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	float t0, t1;
	if (!IntersectCloudShell(cameraPosition, viewDirection, t0, t1))
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	float maxTrace = GetCloudMaxTraceDistance();
	float traceStart = max(t0, 0.0);
	float traceEnd = min(t1, min(sceneDistance, maxTrace));
	if (traceStart >= maxTrace || traceEnd <= traceStart)
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	float distanceFade = GetCloudDistanceFade(traceStart);
	if (distanceFade <= 0.0001)
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	uint marchSteps = uint(clamp(u_Atmosphere.CloudRender1.y, 16.0, 128.0));
	float baseStepLength = (traceEnd - traceStart) / float(marchSteps);

	float cloudRenderScale = max(u_Atmosphere.CloudRender1.x, 1.0);
	vec2 cloudResolution = max(u_ScreenData.FullResolution / cloudRenderScale, vec2(1.0));
	float jitter = Hash12(v_ClipPosition * cloudResolution + vec2(float(u_Atmosphere.Steps.z % 256u), 41.0));

	vec3 sunDirection = GetAtmosphereSunDirection();
	vec3 sunColor = u_Scene.DirectionalLights.Radiance * max(u_Scene.DirectionalLights.Multiplier, 0.0) * u_Atmosphere.SunParams.x;
	vec3 ambientSky = EvaluateSkyAtmosphere(vec3(0.0, 1.0, 0.0)) * max(u_Atmosphere.CloudLighting0.w, 0.0);
	float cosTheta = dot(viewDirection, sunDirection);
	float phase = CloudPhase(cosTheta);

	float extinction = max(u_Atmosphere.CloudLighting1.x, 1.0e-4);
	float scatterMul = max(u_Atmosphere.CloudLighting1.y, 0.0);
	float powderStrength = clamp(u_Atmosphere.CloudLighting1.w, 0.0, 2.0);
	float msAmount = clamp(u_Atmosphere.CloudLighting2.w, 0.0, 1.0);
	vec3 albedo = max(u_Atmosphere.CloudLighting0.xyz, vec3(0.0));

	vec3 scatteredLight = vec3(0.0);
	float transmittance = 1.0;
	float firstHitDistance = LUX_CLOUD_MAX_DEPTH;
	float lastHitDistance = LUX_CLOUD_MAX_DEPTH;

	// Empty-space skipping: when we hit a void, take larger steps until we
	// re-enter cloud, then refine.
	int emptySteps = 0;

	for (uint i = 0u; i < 128u; i++)
	{
		if (i >= marchSteps || transmittance < 0.01)
			break;

		float stepScale = emptySteps > 2 ? 2.0 : 1.0;
		float t = traceStart + (float(i) + jitter) * baseStepLength;
		if (t >= traceEnd)
			break;

		vec3 samplePosition = cameraPosition + viewDirection * t;
		float lod = GetCloudLODFactor(t);
		float density = SampleCloudDensity(samplePosition, lod, false);

		if (density <= LUX_CLOUD_MIN_DENSITY)
		{
			emptySteps++;
			continue;
		}
		emptySteps = 0;

		if (firstHitDistance >= LUX_CLOUD_MAX_DEPTH)
			firstHitDistance = t;
		lastHitDistance = t;

		// Sun visibility via cone march, with multiple-scattering octaves.
		float sunOpticalDepth = SampleSunOpticalDepth(samplePosition, sunDirection);
		float heightFraction = clamp(GetAtmosphereAltitude(samplePosition) * 0.0001, 0.0, 1.0);

		vec3 sunLuminance = vec3(0.0);
		float a = 1.0; // extinction attenuation per octave
		float b = 1.0; // scatter attenuation per octave
		float c = 1.0; // phase attenuation per octave
		for (uint octave = 0u; octave < 3u; octave++)
		{
			float lightTransmittance = exp(-sunOpticalDepth * extinction * a);
			float octavePhase = mix(phase, 1.0 / (4.0 * PI), 1.0 - c);
			sunLuminance += b * sunColor * lightTransmittance * octavePhase;
			a *= 0.5;
			b *= msAmount;
			c *= 0.5;
			if (octave == 0u && msAmount <= 0.0)
				break;
		}

		// Powder term darkens the cloud cores facing the light.
		float powder = 1.0 - exp(-density * 2.0 * powderStrength);
		powder = mix(1.0, powder, 0.6);

		vec3 ambient = ambientSky * (0.4 + 0.6 * heightFraction);
		vec3 luminance = (sunLuminance * powder * scatterMul + ambient) * albedo;

		// Energy-conserving scattering integration (Hillaire).
		float sigmaT = density * extinction;
		float stepLength = baseStepLength * stepScale;
		float stepTransmittance = exp(-sigmaT * stepLength);
		vec3 integScatter = (luminance * density - luminance * density * stepTransmittance) / max(sigmaT, 1.0e-5);
		scatteredLight += transmittance * integScatter;
		transmittance *= stepTransmittance;
	}

	float rawAlpha = clamp((1.0 - transmittance) * distanceFade, 0.0, 1.0);
	if (rawAlpha <= 0.0001 || firstHitDistance >= LUX_CLOUD_MAX_DEPTH)
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	vec3 color = max(scatteredLight, vec3(0.0));
	o_Color = vec4(color, rawAlpha);
	o_Depth = vec4(
		min(firstHitDistance, LUX_CLOUD_MAX_DEPTH),
		min(sceneDistance, LUX_CLOUD_MAX_DEPTH),
		min(traceStart, LUX_CLOUD_MAX_DEPTH),
		min(max(lastHitDistance, firstHitDistance), LUX_CLOUD_MAX_DEPTH));
}
