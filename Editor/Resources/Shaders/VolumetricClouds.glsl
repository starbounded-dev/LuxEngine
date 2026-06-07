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

bool IntersectCloudLayer(vec3 origin, vec3 direction, out float t0, out float t1)
{
	float bottom = u_Atmosphere.CloudParams0.z;
	float top = bottom + max(u_Atmosphere.CloudParams0.w, 1.0);

	if (abs(direction.y) < 0.0001)
		return false;

	float a = (bottom - origin.y) / direction.y;
	float b = (top - origin.y) / direction.y;
	t0 = min(a, b);
	t1 = max(a, b);
	t0 = max(t0, 0.0);
	return t1 > t0;
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
	float targetStepLength = mix(140.0, 420.0, lodFactor);
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

float TraceCloudShadow(vec3 worldPosition, vec3 sunDirection, float sampleDistance, float lodFactor)
{
	float shadowTraceDistance = max(u_Atmosphere.CloudRenderParams.w, 0.0);
	if (shadowTraceDistance <= 0.0 || sampleDistance > shadowTraceDistance)
		return 1.0;

	float t0;
	float t1;
	if (!IntersectCloudLayer(worldPosition + sunDirection * 25.0, sunDirection, t0, t1))
		return 1.0;

	uint requestedShadowSteps = clamp(u_Atmosphere.Steps.y, 0u, 16u);
	if (requestedShadowSteps == 0u)
		return 1.0;

	float shadowLOD = max(lodFactor, smoothstep(0.0, shadowTraceDistance, sampleDistance));
	uint shadowSteps = uint(round(mix(float(requestedShadowSteps), 1.0, shadowLOD)));
	shadowSteps = clamp(shadowSteps, 1u, requestedShadowSteps);
	float stepLength = min((t1 - t0) / float(shadowSteps), 250.0);
	float transmittance = 1.0;

	for (uint i = 0u; i < 16u; i++)
	{
		if (i >= shadowSteps)
			break;

		vec3 p = worldPosition + sunDirection * (t0 + (float(i) + 0.5) * stepLength);
		float density = EvaluateCloudDensityLOD(p, max(shadowLOD, 0.65));
		transmittance *= exp(-density * u_Atmosphere.CloudParams2.z * stepLength * 0.001);
	}

	return clamp(transmittance, 0.0, 1.0);
}

void main()
{
	if (u_Atmosphere.Flags.y == 0u)
	{
		o_Color = vec4(0.0);
		return;
	}

	float depth = texture(sampler2D(u_DepthTexture, r_PointSampler), v_TexCoord).r;
	vec3 cameraPosition = GetAtmosphereCameraPosition();
	vec3 viewDirection = GetAtmosphereViewDirection(v_ClipPosition);
	float sceneDistance = 100000000.0;
	if (depth > 0.000001)
	{
		vec3 scenePosition = ReconstructAtmosphereWorldPosition(v_ClipPosition, depth);
		sceneDistance = length(scenePosition - cameraPosition);
	}

	float t0;
	float t1;
	if (!IntersectCloudLayer(cameraPosition, viewDirection, t0, t1))
	{
		o_Color = vec4(0.0);
		return;
	}

	float maxTraceDistance = GetCloudMaxTraceDistance();
	if (t0 >= maxTraceDistance)
	{
		o_Color = vec4(0.0);
		return;
	}

	t1 = min(t1, min(sceneDistance, maxTraceDistance));
	if (t1 <= t0)
	{
		o_Color = vec4(0.0);
		return;
	}

	float distanceFade = GetCloudDistanceFade(t0);
	if (distanceFade <= 0.0001)
	{
		o_Color = vec4(0.0);
		return;
	}

	uint marchSteps = GetCloudMarchStepCount(t0, t1);
	float stepLength = (t1 - t0) / float(marchSteps);
	float jitter = Hash12(v_ClipPosition * u_ScreenData.FullResolution + vec2(float(u_Atmosphere.Steps.z % 256u), 41.0));
	vec3 sunDirection = GetAtmosphereSunDirection();
	vec3 sunColor = u_Scene.DirectionalLights.Radiance * max(u_Scene.DirectionalLights.Multiplier, 0.0) * u_Atmosphere.SunParams.x;
	vec3 ambientSky = EvaluateSkyAtmosphere(vec3(0.0, 1.0, 0.0)) * u_Atmosphere.CloudColor.w;

	vec3 accumulatedLight = vec3(0.0);
	float transmittance = 1.0;

	for (uint i = 0u; i < 128u; i++)
	{
		if (i >= marchSteps || transmittance < 0.01)
			break;

		float t = t0 + (float(i) + jitter) * stepLength;
		vec3 samplePosition = cameraPosition + viewDirection * t;
		float sampleLOD = GetCloudLODFactor(t);
		float density = EvaluateCloudDensityLOD(samplePosition, sampleLOD);
		if (density <= 0.001)
			continue;

		float powder = 1.0 - exp(-density * stepLength * 0.0025);
		float shadow = TraceCloudShadow(samplePosition, sunDirection, t, sampleLOD);
		float silver = pow(max(dot(viewDirection, sunDirection), 0.0), 16.0) * u_Atmosphere.CloudParams2.w;
		vec3 lighting = ambientSky + sunColor * (shadow + silver) * powder;
		vec3 scattering = lighting * u_Atmosphere.CloudColor.xyz * density;
		float extinction = density * max(u_Atmosphere.CloudParams2.z, 0.001) * stepLength * 0.001;
		float alpha = 1.0 - exp(-extinction);

		accumulatedLight += transmittance * scattering * alpha;
		transmittance *= 1.0 - alpha;
	}

	float rawAlpha = clamp(1.0 - transmittance, 0.0, 1.0);
	vec3 color = rawAlpha > 0.0001 ? accumulatedLight / rawAlpha : vec3(0.0);
	o_Color = vec4(color, rawAlpha * distanceFade);
}
