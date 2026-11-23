// -------------------------------------------------
// -- Lux path tracer (editor reference renderer) --
// -------------------------------------------------
//
// Unidirectional path tracer over a TLAS using ray queries (GL_EXT_ray_query), one pixel per
// invocation, accumulating a running mean in an RGBA32F image and writing the HDR result to an
// RGBA16F image the renderer copies into scene colour. Opt-in (SceneRendererOptions::EnablePathTracer);
// editor-only; see docs/GI_PLAN.md.
//
// Lighting: next-event estimation to one light per bounce (directional, point, spot, rect area),
// BSDF-sampled continuation (cosine diffuse + GGX specular mixture), constant sky/ground environment on
// escape. BRDF terms follow PBR.glslh (GGX + correlated Smith + Frostbite diffuse) so a path-traced
// frame and the raster frame agree on direct lighting. Textures are not sampled: albedo is the
// material's constant colour (a documented v1 limit).
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Identical to the Camera block in Buffers.glslh / ClusterBuild.glsl (same (set,binding), same name).
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

struct RTInstance
{
	vec4 NormalRow0;     // rows of the inverse-transpose of the object-to-world 3x3 (w unused)
	vec4 NormalRow1;
	vec4 NormalRow2;
	uvec4 Info;          // x = first vertex of the BLAS in RTVertices, y = first index in RTIndices
	vec4 AlbedoMetal;    // rgb albedo, a metalness
	vec4 EmissionRough;  // rgb emitted radiance, a roughness
};

layout(set = 1, binding = 60) uniform accelerationStructureEXT u_RTScene;
layout(std430, set = 1, binding = 61) readonly buffer RTVertices { vec4 Vertices[]; };   // [2i] position, [2i+1] normal
layout(std430, set = 1, binding = 62) readonly buffer RTIndices { uint Indices[]; };
layout(std430, set = 1, binding = 63) readonly buffer RTInstances { RTInstance Instances[]; };
layout(std430, set = 1, binding = 64) readonly buffer RTLights { vec4 Lights[]; };       // 4 vec4 per light
layout(rgba32f, set = 1, binding = 65) uniform image2D o_PathTraceAccum;
layout(rgba16f, set = 1, binding = 66) uniform writeonly image2D o_PathTraceImage;

layout(push_constant) uniform PathTracePush
{
	uvec4 SizeFrameBounces; // width, height, frame index (samples already accumulated), max bounces
	uvec4 Counts;           // light count, flags (bit 0: uniform-environment debug), 0, 0
	vec4 SkyRadiance;       // rgb sky (upper hemisphere), a unused
	vec4 GroundRadiance;    // rgb ground (lower hemisphere)
} u_Push;

const float PI = 3.14159265359;
const float MIN_ALPHA = 0.0064;
const uint FLAG_UNIFORM_ENV = 1u;

// ---- random -----------------------------------------------------------------------------------
uint g_Rng;
uint PCG(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}
float Rand()
{
	g_Rng = PCG(g_Rng);
	return float(g_Rng >> 8) * (1.0 / 16777216.0);
}

// ---- BRDF (PBR.glslh) -------------------------------------------------------------------------
float DistributionGGX(float NdotH, float alpha)
{
	float a2 = alpha * alpha;
	float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}
float VisibilitySmith(float NdotV, float NdotL, float alpha)
{
	float a2 = alpha * alpha;
	float lv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
	float ll = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
	return 0.5 / max(lv + ll, 1e-7);
}
vec3 DiffuseFrostbite(vec3 albedo, float roughness, float NdotV, float NdotL, float LdotH)
{
	float energyBias = mix(0.0, 0.5, roughness);
	float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
	float fd90 = energyBias + 2.0 * LdotH * LdotH * roughness;
	float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
	float viewScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
	return albedo * (lightScatter * viewScatter * energyFactor / PI);
}

struct Surface
{
	vec3 P;
	vec3 N;       // shading normal, facing the viewer
	vec3 Albedo;
	float Metal;
	float Rough;
	vec3 Emission;
	vec3 F0;
};

// BRDF * cos(theta_l) for light direction L, view V.
vec3 EvalBRDF(Surface s, vec3 V, vec3 L)
{
	float NdotL = dot(s.N, L);
	if (NdotL <= 0.0)
		return vec3(0.0);
	vec3 H = normalize(L + V);
	float NdotV = max(dot(s.N, V), 1e-4);
	float NdotH = max(dot(s.N, H), 0.0);
	float VdotH = max(dot(V, H), 0.0);
	float LdotH = max(dot(L, H), 0.0);
	float alpha = max(s.Rough * s.Rough, MIN_ALPHA);
	vec3 F = s.F0 + (1.0 - s.F0) * pow(1.0 - VdotH, 5.0);
	vec3 spec = F * (DistributionGGX(NdotH, alpha) * VisibilitySmith(NdotV, NdotL, alpha));
	vec3 diffuseColor = s.Albedo * (1.0 - s.Metal);
	vec3 diff = DiffuseFrostbite(diffuseColor, s.Rough, NdotV, NdotL, LdotH) * (vec3(1.0) - F);
	return (diff + spec) * NdotL;
}

float SpecProbability(Surface s)
{
	return clamp(0.05 + dot(s.F0, vec3(0.3333)) + (1.0 - s.Rough) * 0.25, 0.05, 0.95);
}

// pdf (solid angle) of sampling L: mixture of cosine diffuse and GGX NDF sampling.
float BSDFPdf(Surface s, vec3 V, vec3 L)
{
	float NdotL = dot(s.N, L);
	if (NdotL <= 0.0)
		return 0.0;
	vec3 H = normalize(L + V);
	float NdotH = max(dot(s.N, H), 0.0);
	float VdotH = max(dot(V, H), 1e-4);
	float alpha = max(s.Rough * s.Rough, MIN_ALPHA);
	float pSpec = SpecProbability(s);
	float pdfDiff = NdotL / PI;
	float pdfSpec = DistributionGGX(NdotH, alpha) * NdotH / (4.0 * VdotH);
	return (1.0 - pSpec) * pdfDiff + pSpec * pdfSpec;
}

void Basis(vec3 n, out vec3 t, out vec3 b)
{
	vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	t = normalize(cross(up, n));
	b = cross(n, t);
}

bool SampleBSDF(Surface s, vec3 V, out vec3 L)
{
	vec3 t, b;
	Basis(s.N, t, b);
	if (Rand() < SpecProbability(s))
	{
		float alpha = max(s.Rough * s.Rough, MIN_ALPHA);
		float u1 = Rand(), u2 = Rand();
		float cosT = sqrt((1.0 - u1) / (1.0 + (alpha * alpha - 1.0) * u1));
		float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
		float phi = 2.0 * PI * u2;
		vec3 H = normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + s.N * cosT);
		L = reflect(-V, H);
	}
	else
	{
		float u1 = Rand(), u2 = Rand();
		float r = sqrt(u1);
		float phi = 2.0 * PI * u2;
		L = normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + s.N * sqrt(max(1.0 - u1, 0.0)));
	}
	return dot(L, s.N) > 0.0;
}

// ---- scene queries ----------------------------------------------------------------------------
bool Occluded(vec3 o, vec3 d, float tMax)
{
	rayQueryEXT q;
	rayQueryInitializeEXT(q, u_RTScene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF, o, 1e-4, d, tMax);
	while (rayQueryProceedEXT(q)) {}
	return rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

bool TraceClosest(vec3 o, vec3 d, out Surface s)
{
	rayQueryEXT q;
	rayQueryInitializeEXT(q, u_RTScene, gl_RayFlagsOpaqueEXT, 0xFF, o, 1e-4, d, 1.0e30);
	while (rayQueryProceedEXT(q)) {}
	if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionTriangleEXT)
		return false;

	const float t = rayQueryGetIntersectionTEXT(q, true);
	const uint instanceIndex = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(q, true));
	const uint prim = uint(rayQueryGetIntersectionPrimitiveIndexEXT(q, true));
	const vec2 bary = rayQueryGetIntersectionBarycentricsEXT(q, true);
	const vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);

	RTInstance inst = Instances[instanceIndex];
	uint i0 = inst.Info.x + Indices[inst.Info.y + prim * 3u + 0u];
	uint i1 = inst.Info.x + Indices[inst.Info.y + prim * 3u + 1u];
	uint i2 = inst.Info.x + Indices[inst.Info.y + prim * 3u + 2u];
	vec3 nObj = normalize(Vertices[2u * i0 + 1u].xyz * w.x + Vertices[2u * i1 + 1u].xyz * w.y + Vertices[2u * i2 + 1u].xyz * w.z);
	vec3 n = normalize(vec3(dot(inst.NormalRow0.xyz, nObj), dot(inst.NormalRow1.xyz, nObj), dot(inst.NormalRow2.xyz, nObj)));
	// Face the ray: the surface is shaded from the side the ray arrives on.
	if (dot(n, d) > 0.0)
		n = -n;

	s.P = o + d * t;
	s.N = n;
	s.Albedo = inst.AlbedoMetal.rgb;
	s.Metal = inst.AlbedoMetal.a;
	s.Rough = clamp(inst.EmissionRough.a, 0.04, 1.0);
	s.Emission = inst.EmissionRough.rgb;
	s.F0 = mix(vec3(0.04), s.Albedo, s.Metal);
	return true;
}

vec3 Environment(vec3 d)
{
	if ((u_Push.Counts.y & FLAG_UNIFORM_ENV) != 0u)
		return u_Push.SkyRadiance.rgb;
	return mix(u_Push.GroundRadiance.rgb, u_Push.SkyRadiance.rgb, clamp(0.5 + 0.5 * d.y, 0.0, 1.0));
}

// Direct light from one randomly chosen light. Returns radiance * BRDF * cos / pdf.
vec3 DirectLight(Surface s, vec3 V)
{
	const uint lightCount = u_Push.Counts.x;
	if (lightCount == 0u || (u_Push.Counts.y & FLAG_UNIFORM_ENV) != 0u)
		return vec3(0.0);

	const uint index = min(uint(Rand() * float(lightCount)), lightCount - 1u);
	const vec4 a = Lights[4u * index + 0u];
	const vec4 b = Lights[4u * index + 1u];
	const vec4 c = Lights[4u * index + 2u];
	const vec4 dd = Lights[4u * index + 3u];
	const int type = int(a.w + 0.5);
	const vec3 origin = s.P + s.N * 1e-3;

	vec3 L;
	float dist = 1.0e30;
	vec3 radiance = vec3(0.0);
	if (type == 0) // directional: a.xyz toward the light, b.rgb radiance * intensity
	{
		L = normalize(a.xyz);
		radiance = b.rgb;
	}
	else if (type == 3) // rect area: centre a.xyz, half-extents c.xyz / dd.xyz, emits along -normalize(cross(c, dd))
	{
		vec3 p = a.xyz + (2.0 * Rand() - 1.0) * c.xyz + (2.0 * Rand() - 1.0) * dd.xyz;
		vec3 toLight = p - s.P;
		float d2 = max(dot(toLight, toLight), 1e-6);
		dist = sqrt(d2);
		L = toLight / dist;
		vec3 areaCross = cross(c.xyz, dd.xyz);
		float areaLen = length(areaCross);
		vec3 emitDir = -areaCross / max(areaLen, 1e-8);
		float cosLight = max(dot(emitDir, -L), 0.0);
		radiance = b.rgb * (cosLight * 4.0 * areaLen / d2);
	}
	else // point (1) / spot (2): a.xyz position, b.rgb radiance * intensity, b.a range
	{
		vec3 toLight = a.xyz - s.P;
		float d2 = max(dot(toLight, toLight), 0.01);
		dist = sqrt(d2);
		L = toLight / dist;
		float ratio = d2 / max(b.a * b.a, 1e-4);
		float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
		float atten = (window * window) / d2;
		if (type == 2) // spot: c.xyz direction, c.w cos(half angle)
		{
			float cs = dot(-L, normalize(c.xyz));
			atten *= clamp((cs - c.w) / max(1.0 - c.w, 1e-3), 0.0, 1.0);
		}
		radiance = b.rgb * atten;
	}

	if (dot(s.N, L) <= 0.0 || max(radiance.r, max(radiance.g, radiance.b)) <= 0.0)
		return vec3(0.0);
	if (Occluded(origin, L, dist - 2e-3))
		return vec3(0.0);
	return EvalBRDF(s, V, L) * radiance * float(lightCount);
}

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if (pixel.x >= u_Push.SizeFrameBounces.x || pixel.y >= u_Push.SizeFrameBounces.y)
		return;

	const uint frame = u_Push.SizeFrameBounces.z;
	g_Rng = PCG(pixel.x + PCG(pixel.y + PCG(frame * 9781u + 1u)));

	// Primary ray with a box-filtered sub-pixel jitter.
	vec2 uv = (vec2(pixel) + vec2(Rand(), Rand())) / vec2(u_Push.SizeFrameBounces.xy);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 viewPoint = u_Camera.InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
	vec3 viewDir = normalize(viewPoint.xyz / viewPoint.w);
	vec3 origin = u_Camera.InverseViewMatrix[3].xyz;
	vec3 dir = normalize((u_Camera.InverseViewMatrix * vec4(viewDir, 0.0)).xyz);

	vec3 radiance = vec3(0.0);
	vec3 throughput = vec3(1.0);
	const uint maxBounces = u_Push.SizeFrameBounces.w;

	for (uint bounce = 0u; bounce <= maxBounces; bounce++)
	{
		Surface s;
		if (!TraceClosest(origin, dir, s))
		{
			radiance += throughput * Environment(dir);
			break;
		}

		radiance += throughput * s.Emission;

		vec3 V = -dir;
		radiance += throughput * DirectLight(s, V);

		if (bounce == maxBounces)
			break;

		vec3 L;
		if (!SampleBSDF(s, V, L))
			break;
		float pdf = BSDFPdf(s, V, L);
		if (pdf <= 1e-6)
			break;
		throughput *= EvalBRDF(s, V, L) / pdf;

		// Russian roulette after a few bounces.
		if (bounce >= 2u)
		{
			float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
			if (Rand() > p)
				break;
			throughput /= p;
		}

		origin = s.P + s.N * 1e-3;
		dir = L;
	}

	// Firefly clamp (a documented bias): single samples above this are scaled down, preserving hue.
	const float maxSample = 64.0;
	float peak = max(radiance.r, max(radiance.g, radiance.b));
	if (peak > maxSample)
		radiance *= maxSample / peak;

	vec3 previous = frame == 0u ? vec3(0.0) : imageLoad(o_PathTraceAccum, ivec2(pixel)).rgb;
	vec3 mean = (previous * float(frame) + radiance) / float(frame + 1u);
	imageStore(o_PathTraceAccum, ivec2(pixel), vec4(mean, 1.0));
	imageStore(o_PathTraceImage, ivec2(pixel), vec4(mean, 1.0));
}
