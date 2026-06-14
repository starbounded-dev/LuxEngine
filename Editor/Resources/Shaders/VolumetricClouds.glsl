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

#include <Samplers.glslh>
#include <Atmosphere.glslh>

layout(set = 1, binding = 1) uniform texture2D u_DepthTexture;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec2 v_ClipPosition;
layout(location = 0) out vec4 o_Color;
layout(location = 1) out vec4 o_Depth;

const float LUX_CLOUD_MAX_DEPTH = 65000.0;
const float LUX_CLOUD_MIN_DENSITY = 0.001;

void StoreEmptyCloud(float sceneDistance)
{
	o_Color = vec4(0.0);
	o_Depth = vec4(LUX_CLOUD_MAX_DEPTH, min(sceneDistance, LUX_CLOUD_MAX_DEPTH), LUX_CLOUD_MAX_DEPTH, LUX_CLOUD_MAX_DEPTH);
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

void SortFive(inout float values[5])
{
	for (int i = 1; i < 5; ++i)
	{
		float value = values[i];
		int j = i - 1;
		while (j >= 0 && values[j] > value)
		{
			values[j + 1] = values[j];
			--j;
		}
		values[j + 1] = value;
	}
}

bool IntersectCloudLayer(vec3 origin, vec3 direction, out float t0, out float t1)
{
	float planetRadius = GetAtmospherePlanetRadiusMeters();
	vec3 planetCenter = GetAtmospherePlanetCenter();
	float bottomRadius = planetRadius + u_Atmosphere.CloudParams0.z;
	float topRadius = bottomRadius + max(u_Atmosphere.CloudParams0.w, 1.0);

	vec2 topHit;
	if (!IntersectSphere(origin, direction, planetCenter, topRadius, topHit))
		return false;

	vec2 bottomHit = vec2(LUX_CLOUD_MAX_DEPTH);
	IntersectSphere(origin, direction, planetCenter, bottomRadius, bottomHit);

	float cuts[5];
	cuts[0] = 0.0;
	cuts[1] = topHit.x;
	cuts[2] = topHit.y;
	cuts[3] = bottomHit.x;
	cuts[4] = bottomHit.y;
	SortFive(cuts);

	for (int i = 0; i < 4; ++i)
	{
		float segmentStart = max(cuts[i], 0.0);
		float segmentEnd = cuts[i + 1];
		if (segmentEnd <= segmentStart)
			continue;

		float segmentMid = 0.5 * (segmentStart + segmentEnd);
		float radius = length(origin + direction * segmentMid - planetCenter);
		if (radius >= bottomRadius && radius <= topRadius)
		{
			t0 = segmentStart;
			t1 = segmentEnd;
			return t1 > t0;
		}
	}

	return false;
}

float GetCloudMaxTraceDistance()
{
	return max(u_Atmosphere.CloudRenderParams.x, 1.0);
}

float GetCloudLODFactor(float distanceToCamera)
{
	float maxTraceDistance = GetCloudMaxTraceDistance();
	float lodStart = clamp(u_Atmosphere.CloudRenderParams.z, 0.0, maxTraceDistance);
	return smoothstep(lodStart, maxTraceDistance, clamp(distanceToCamera, 0.0, maxTraceDistance));
}

uint GetCloudMarchStepCount(float t0, float t1)
{
	uint requestedSteps = clamp(u_Atmosphere.Steps.x, 8u, 128u);
	uint minimumSteps = max(8u, requestedSteps / 4u);
	float rayDistance = clamp((t0 + t1) * 0.5, 0.0, GetCloudMaxTraceDistance());
	float lodFactor = GetCloudLODFactor(rayDistance);
	uint lodSteps = uint(round(mix(float(requestedSteps), float(minimumSteps), lodFactor)));

	float rayLength = max(t1 - t0, 1.0);
	float targetStepLength = mix(120.0, 460.0, lodFactor);
	uint lengthLimitedSteps = uint(clamp(ceil(rayLength / targetStepLength), float(minimumSteps), float(requestedSteps)));

	return clamp(min(lodSteps, lengthLimitedSteps), minimumSteps, requestedSteps);
}

float GetCloudDistanceFade(float t0)
{
	float maxTraceDistance = GetCloudMaxTraceDistance();
	float fadeDistance = clamp(u_Atmosphere.CloudRenderParams.y, 0.0, maxTraceDistance);
	if (fadeDistance <= 0.0001)
		return t0 < maxTraceDistance ? 1.0 : 0.0;

	float fadeStart = max(maxTraceDistance - fadeDistance, 0.0);
	return 1.0 - smoothstep(fadeStart, maxTraceDistance, clamp(t0, 0.0, maxTraceDistance));
}

float PhaseHG(float cosTheta, float g)
{
	g = clamp(g, -0.95, 0.95);
	float g2 = g * g;
	float denominator = max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5), 0.0001);
	return (1.0 - g2) / (4.0 * PI * denominator);
}

float CloudPhase(float cosTheta)
{
	float forwardPhase = PhaseHG(cosTheta, 0.62) * (4.0 * PI);
	float softPhase = mix(1.0, PhaseHG(cosTheta, 0.20) * (4.0 * PI), 0.35);
	float silver = pow(max(cosTheta, 0.0), 18.0) * u_Atmosphere.CloudParams2.w;
	return max(mix(softPhase, forwardPhase, 0.72) + silver, 0.0);
}

float TraceCloudShadow(vec3 worldPosition, vec3 sunDirection, float sampleDistance, float lodFactor)
{
	float shadowTraceDistance = max(u_Atmosphere.CloudRenderParams.w, 0.0);
	if (shadowTraceDistance <= 0.0 || sampleDistance > shadowTraceDistance)
		return 1.0;

	float t0;
	float t1;
	if (!IntersectCloudLayer(worldPosition + sunDirection * 20.0, sunDirection, t0, t1))
		return 1.0;

	t1 = min(t1, shadowTraceDistance);
	if (t1 <= t0)
		return 1.0;

	uint requestedShadowSteps = clamp(u_Atmosphere.Steps.y, 0u, 16u);
	if (requestedShadowSteps == 0u)
		return 1.0;

	float shadowLOD = max(lodFactor, smoothstep(0.0, shadowTraceDistance, sampleDistance));
	uint shadowSteps = uint(round(mix(float(requestedShadowSteps), 1.0, shadowLOD)));
	shadowSteps = clamp(shadowSteps, 1u, requestedShadowSteps);
	float stepLength = min((t1 - t0) / float(shadowSteps), 280.0);
	float transmittance = 1.0;

	for (uint i = 0u; i < 16u; i++)
	{
		if (i >= shadowSteps || transmittance < 0.05)
			break;

		vec3 p = worldPosition + sunDirection * (t0 + (float(i) + 0.5) * stepLength);
		float density = EvaluateCloudDensityLOD(p, max(shadowLOD, 0.65));
		float extinction = density * max(u_Atmosphere.CloudParams2.z, 0.001) * stepLength * 0.001;
		transmittance *= exp(-extinction);
	}

	return clamp(transmittance, 0.0, 1.0);
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

	float t0;
	float t1;
	if (!IntersectCloudLayer(cameraPosition, viewDirection, t0, t1))
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	float maxTraceDistance = GetCloudMaxTraceDistance();
	if (t0 >= maxTraceDistance)
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	float traceStart = t0;
	float traceEnd = min(t1, min(sceneDistance, maxTraceDistance));
	if (traceEnd <= traceStart)
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

	uint marchSteps = GetCloudMarchStepCount(traceStart, traceEnd);
	float stepLength = (traceEnd - traceStart) / float(marchSteps);
	float cloudRenderScale = max(u_Atmosphere.CloudRenderParams2.x, 1.0);
	vec2 cloudResolution = max(u_ScreenData.FullResolution / cloudRenderScale, vec2(1.0));
	float jitter = Hash12(v_ClipPosition * cloudResolution + vec2(float(u_Atmosphere.Steps.z % 256u), 41.0));

	vec3 sunDirection = GetAtmosphereSunDirection();
	vec3 sunColor = u_Scene.DirectionalLights.Radiance * max(u_Scene.DirectionalLights.Multiplier, 0.0) * u_Atmosphere.SunParams.x;
	vec3 ambientSky = EvaluateSkyAtmosphere(vec3(0.0, 1.0, 0.0)) * max(u_Atmosphere.CloudColor.w, 0.0);
	float phase = CloudPhase(dot(viewDirection, sunDirection));
	float multiScattering = clamp(u_Atmosphere.SunParams.z, 0.0, 2.0);

	vec3 accumulatedLight = vec3(0.0);
	float viewTransmittance = 1.0;
	float firstHitDistance = LUX_CLOUD_MAX_DEPTH;
	float lastHitDistance = LUX_CLOUD_MAX_DEPTH;
	float cachedShadow = 1.0;
	uint shadowReuseInterval = u_Atmosphere.Steps.y <= 1u ? 4u : 2u;

	for (uint i = 0u; i < 128u; i++)
	{
		if (i >= marchSteps || viewTransmittance < 0.01)
			break;

		float t = traceStart + (float(i) + jitter) * stepLength;
		vec3 samplePosition = cameraPosition + viewDirection * t;
		float sampleLOD = GetCloudLODFactor(t);
		float density = EvaluateCloudDensityLOD(samplePosition, sampleLOD);
		if (density <= LUX_CLOUD_MIN_DENSITY)
			continue;

		if (firstHitDistance >= LUX_CLOUD_MAX_DEPTH)
			firstHitDistance = t;
		lastHitDistance = t;

		if ((i % shadowReuseInterval) == 0u)
			cachedShadow = TraceCloudShadow(samplePosition, sunDirection, t, sampleLOD);

		float extinction = density * max(u_Atmosphere.CloudParams2.z, 0.001) * 0.001;
		float stepTransmittance = exp(-extinction * stepLength);
		float stepOpacity = clamp(1.0 - stepTransmittance, 0.0, 1.0);
		float powder = 1.0 - exp(-density * stepLength * 0.002);

		vec3 directionalLighting = sunColor * cachedShadow * phase * (0.65 + 0.65 * powder);
		vec3 multipleScattering = sunColor * (1.0 - cachedShadow) * multiScattering * 0.12;
		vec3 ambientLighting = ambientSky * (0.30 + 0.45 * multiScattering);
		vec3 lighting = directionalLighting + multipleScattering + ambientLighting;
		vec3 scattering = lighting * max(u_Atmosphere.CloudColor.xyz, vec3(0.0)) * density;

		accumulatedLight += viewTransmittance * scattering * stepOpacity;
		viewTransmittance *= stepTransmittance;
	}

	float rawAlpha = clamp((1.0 - viewTransmittance) * distanceFade, 0.0, 1.0);
	if (rawAlpha <= 0.0001 || firstHitDistance >= LUX_CLOUD_MAX_DEPTH)
	{
		StoreEmptyCloud(sceneDistance);
		return;
	}

	vec3 color = max(accumulatedLight / max(1.0 - viewTransmittance, 0.0001), vec3(0.0));
	o_Color = vec4(color, rawAlpha);
	o_Depth = vec4(
		min(firstHitDistance, LUX_CLOUD_MAX_DEPTH),
		min(sceneDistance, LUX_CLOUD_MAX_DEPTH),
		min(traceStart, LUX_CLOUD_MAX_DEPTH),
		min(max(lastHitDistance, firstHitDistance), LUX_CLOUD_MAX_DEPTH));
}
