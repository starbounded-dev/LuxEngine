// Preetham/Perez analytic sky cubemap generator.
//
// Generates an HDR radiance cubemap used by dynamic SkyLight IBL. The model is
// valid for daylight skies, with guarded horizon and twilight handling so editor
// controls cannot produce NaNs or extreme negative RGB values.

#version 450 core
#pragma stage : comp

const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;
const float SKY_LUMINANCE_SCALE = 0.04;
const float SUN_ANGULAR_RADIUS = 0.00465; // 0.53 degree apparent diameter / 2

layout(binding = 0, rgba32f) restrict writeonly uniform imageCube o_CubeMap;

layout(push_constant) uniform Uniforms
{
	vec3 TurbidityAzimuthInclination;
} u_Uniforms;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

float SafeAcos(float value)
{
	return acos(clamp(value, -1.0, 1.0));
}

float DecodeAngle(float value)
{
	// Existing scenes use radians; the editor range is degree-like. Accept both
	// by treating large magnitudes as degrees while preserving old serialized data.
	return abs(value) > TWO_PI ? radians(value) : value;
}

vec3 GetCubeMapTexCoord()
{
	ivec2 size = imageSize(o_CubeMap);
	vec2 st = (vec2(gl_GlobalInvocationID.xy) + vec2(0.5)) / vec2(size);
	vec2 uv = vec2(2.0 * st.x - 1.0, 1.0 - 2.0 * st.y);

	vec3 direction = vec3(0.0);
	if (gl_GlobalInvocationID.z == 0)      direction = vec3( 1.0, uv.y, -uv.x);
	else if (gl_GlobalInvocationID.z == 1) direction = vec3(-1.0, uv.y,  uv.x);
	else if (gl_GlobalInvocationID.z == 2) direction = vec3( uv.x, 1.0, -uv.y);
	else if (gl_GlobalInvocationID.z == 3) direction = vec3( uv.x, -1.0, uv.y);
	else if (gl_GlobalInvocationID.z == 4) direction = vec3( uv.x, uv.y,  1.0);
	else if (gl_GlobalInvocationID.z == 5) direction = vec3(-uv.x, uv.y, -1.0);

	return normalize(direction);
}

vec3 GetSunDirection(float azimuth, float inclination)
{
	float sinInclination = sin(inclination);
	return normalize(vec3(
		sinInclination * cos(azimuth),
		cos(inclination),
		sinInclination * sin(azimuth)));
}

vec3 YxyToXYZ(vec3 yxy)
{
	float luminance = max(yxy.x, 0.0);
	float x = clamp(yxy.y, 0.0001, 0.9999);
	float y = clamp(yxy.z, 0.0001, 0.9999);

	return vec3(
		x * luminance / y,
		luminance,
		(1.0 - x - y) * luminance / y);
}

vec3 XYZToLinearSRGB(vec3 xyz)
{
	return vec3(
		dot(vec3( 3.2404542, -1.5371385, -0.4985314), xyz),
		dot(vec3(-0.9692660,  1.8760108,  0.0415560), xyz),
		dot(vec3( 0.0556434, -0.2040259,  1.0572252), xyz));
}

vec3 YxyToLinearSRGB(vec3 yxy)
{
	return max(XYZToLinearSRGB(YxyToXYZ(yxy)), vec3(0.0));
}

void CalculatePerezDistribution(float turbidity, out vec3 A, out vec3 B, out vec3 C, out vec3 D, out vec3 E)
{
	A = vec3( 0.1787 * turbidity - 1.4630, -0.0193 * turbidity - 0.2592, -0.0167 * turbidity - 0.2608);
	B = vec3(-0.3554 * turbidity + 0.4275, -0.0665 * turbidity + 0.0008, -0.0950 * turbidity + 0.0092);
	C = vec3(-0.0227 * turbidity + 5.3251, -0.0004 * turbidity + 0.2125, -0.0079 * turbidity + 0.2102);
	D = vec3( 0.1206 * turbidity - 2.5771, -0.0641 * turbidity - 0.8989, -0.0441 * turbidity - 1.6537);
	E = vec3(-0.0670 * turbidity + 0.3703, -0.0033 * turbidity + 0.0452, -0.0109 * turbidity + 0.0529);
}

vec3 CalculateZenithLuminanceYxy(float turbidity, float thetaSun)
{
	float theta2 = thetaSun * thetaSun;
	float theta3 = theta2 * thetaSun;
	float t = turbidity;
	float t2 = t * t;

	float chi = (4.0 / 9.0 - t / 120.0) * (PI - 2.0 * thetaSun);
	float Yz = (4.0453 * t - 4.9710) * tan(chi) - 0.2155 * t + 2.4192;

	float xz =
		( 0.00165 * theta3 - 0.00375 * theta2 + 0.00209 * thetaSun) * t2 +
		(-0.02903 * theta3 + 0.06377 * theta2 - 0.03202 * thetaSun + 0.00394) * t +
		( 0.11693 * theta3 - 0.21196 * theta2 + 0.06052 * thetaSun + 0.25886);

	float yz =
		( 0.00275 * theta3 - 0.00610 * theta2 + 0.00317 * thetaSun) * t2 +
		(-0.04214 * theta3 + 0.08970 * theta2 - 0.04153 * thetaSun + 0.00516) * t +
		( 0.15346 * theta3 - 0.26756 * theta2 + 0.06670 * thetaSun + 0.26688);

	return vec3(max(Yz, 0.0), clamp(xz, 0.0001, 0.9999), clamp(yz, 0.0001, 0.9999));
}

vec3 CalculatePerezLuminanceYxy(float theta, float gamma, vec3 A, vec3 B, vec3 C, vec3 D, vec3 E)
{
	float cosTheta = max(cos(theta), 0.01);
	float cosGamma = cos(gamma);
	vec3 horizonTerm = 1.0 + A * exp(clamp(B / cosTheta, vec3(-64.0), vec3(64.0)));
	vec3 sunTerm = 1.0 + C * exp(clamp(D * gamma, vec3(-64.0), vec3(64.0))) + E * cosGamma * cosGamma;
	return max(horizonTerm * sunTerm, vec3(0.0001));
}

vec3 CalculatePreethamSkyRGB(vec3 sunDirection, vec3 viewDirection, float turbidity)
{
	vec3 A, B, C, D, E;
	CalculatePerezDistribution(turbidity, A, B, C, D, E);

	float thetaSun = SafeAcos(clamp(sunDirection.y, 0.001, 1.0));
	float thetaView = SafeAcos(clamp(viewDirection.y, 0.001, 1.0));
	float gamma = SafeAcos(dot(sunDirection, viewDirection));

	vec3 zenith = CalculateZenithLuminanceYxy(turbidity, thetaSun);
	vec3 numerator = CalculatePerezLuminanceYxy(thetaView, gamma, A, B, C, D, E);
	vec3 denominator = CalculatePerezLuminanceYxy(0.0, thetaSun, A, B, C, D, E);
	vec3 yxy = zenith * numerator / max(denominator, vec3(0.0001));

	return YxyToLinearSRGB(yxy) * SKY_LUMINANCE_SCALE;
}

vec3 CalculateLowerHemisphere(vec3 horizonColor, vec3 viewDirection, vec3 sunDirection, float sunVisibility)
{
	float horizon = clamp(1.0 - abs(viewDirection.y) * 2.0, 0.0, 1.0);
	float sunFacing = pow(max(dot(normalize(vec3(viewDirection.x, 0.0, viewDirection.z) + vec3(0.0001)),
		normalize(vec3(sunDirection.x, 0.0, sunDirection.z) + vec3(0.0001))), 0.0), 4.0);

	vec3 groundTint = mix(vec3(0.025, 0.027, 0.030), horizonColor * vec3(0.34, 0.36, 0.38), 0.65);
	groundTint += horizonColor * horizon * (0.18 + 0.22 * sunFacing * sunVisibility);
	return max(groundTint, vec3(0.0));
}

vec3 CalculateSunDisk(vec3 viewDirection, vec3 sunDirection, float turbidity, float sunVisibility)
{
	float sunCos = dot(viewDirection, sunDirection);
	float disk = smoothstep(cos(SUN_ANGULAR_RADIUS * 1.5), cos(SUN_ANGULAR_RADIUS * 0.65), sunCos);
	float aureoleWide = pow(max(sunCos, 0.0), mix(18.0, 8.0, clamp((turbidity - 2.0) / 8.0, 0.0, 1.0)));
	float aureoleTight = pow(max(sunCos, 0.0), 256.0);

	float elevation = clamp(sunDirection.y * 4.0, 0.0, 1.0);
	vec3 sunColor = mix(vec3(1.0, 0.42, 0.16), vec3(1.0, 0.93, 0.78), elevation);
	float hazeBoost = mix(0.8, 1.9, clamp((turbidity - 2.0) / 8.0, 0.0, 1.0));

	return sunColor * sunVisibility * (disk * 18.0 + aureoleTight * 1.5 + aureoleWide * 0.08 * hazeBoost);
}

void main()
{
	ivec2 size = imageSize(o_CubeMap);
	if (gl_GlobalInvocationID.x >= uint(size.x) || gl_GlobalInvocationID.y >= uint(size.y) || gl_GlobalInvocationID.z >= 6u)
		return;

	float turbidity = clamp(u_Uniforms.TurbidityAzimuthInclination.x, 1.7, 10.0);
	float azimuth = DecodeAngle(u_Uniforms.TurbidityAzimuthInclination.y);
	float inclination = DecodeAngle(u_Uniforms.TurbidityAzimuthInclination.z);

	vec3 sunDirection = GetSunDirection(azimuth, inclination);
	vec3 viewDirection = GetCubeMapTexCoord();

	float sunVisibility = smoothstep(-0.05, 0.025, sunDirection.y);
	float twilightVisibility = smoothstep(-0.32, 0.035, sunDirection.y);

	vec3 skyViewDirection = normalize(vec3(viewDirection.x, max(viewDirection.y, 0.001), viewDirection.z));
	vec3 skyColor = CalculatePreethamSkyRGB(sunDirection, skyViewDirection, turbidity);

	vec3 nightColor = mix(vec3(0.004, 0.006, 0.011), vec3(0.012, 0.020, 0.040), clamp(viewDirection.y * 0.5 + 0.5, 0.0, 1.0));
	skyColor = mix(nightColor, skyColor, twilightVisibility);

	float lowerBlend = 1.0 - smoothstep(-0.06, 0.02, viewDirection.y);
	if (lowerBlend > 0.0)
	{
		vec3 lowerColor = CalculateLowerHemisphere(skyColor, viewDirection, sunDirection, sunVisibility);
		skyColor = mix(skyColor, lowerColor, lowerBlend);
	}

	if (viewDirection.y > -0.015)
		skyColor += CalculateSunDisk(viewDirection, sunDirection, turbidity, sunVisibility);

	imageStore(o_CubeMap, ivec3(gl_GlobalInvocationID), vec4(clamp(skyColor, vec3(0.0), vec3(500.0)), 1.0));
}
