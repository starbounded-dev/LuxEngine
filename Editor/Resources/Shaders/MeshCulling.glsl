#version 450 core
#pragma stage : comp

#include <Samplers.glslh>

layout(std430, set = 1, binding = 0) readonly buffer MeshCullDrawData
{
	uvec4 Draws[];
} r_MeshCullDrawData;

layout(std430, set = 1, binding = 1) readonly buffer ObjectIndexes
{
	uint Indices[];
} r_ObjectIndexes;

struct GPUSceneInstance
{
	vec4 TransformRows[3];
	vec4 PreviousTransformRows[3];
	vec4 BoundsSphere;
	uvec4 Metadata;
	uvec4 ObjectData;
};

layout(std430, set = 1, binding = 2) readonly buffer GPUSceneInstances
{
	GPUSceneInstance Instances[];
} r_GPUSceneInstances;

layout(std430, set = 1, binding = 3) writeonly buffer VisibleObjectIndexes
{
	uint Indices[];
} o_VisibleObjectIndexes;

layout(std430, set = 1, binding = 4) buffer IndirectDrawCommands
{
	uint Data[];
} b_IndirectDrawCommands;

layout(set = 1, binding = 5) uniform texture2D u_HZB;

layout(push_constant) uniform PushConstants
{
	mat4 ViewProjection;
	vec4 HZBUVFactorAndViewportSize;
	uint DrawCount;
	uint FrustumCullingEnabled;
	uint OcclusionCullingEnabled;
	uint NumDepthMips;
	float DepthBias;
	float BoundsScale;
	uint _Padding0;
	uint _Padding1;
} u_PushConstants;

vec4 NormalizePlane(vec4 plane)
{
	float lengthSq = dot(plane.xyz, plane.xyz);
	if (lengthSq <= 0.0)
		return plane;

	return plane * inversesqrt(lengthSq);
}

bool IsSphereVisible(vec4 sphere)
{
	if (u_PushConstants.FrustumCullingEnabled == 0)
		return true;

	mat4 vp = u_PushConstants.ViewProjection;
	vec4 planes[6];
	planes[0] = NormalizePlane(vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]));
	planes[1] = NormalizePlane(vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]));
	planes[2] = NormalizePlane(vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]));
	planes[3] = NormalizePlane(vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]));
	planes[4] = NormalizePlane(vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]));
	planes[5] = NormalizePlane(vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]));

	for (int i = 0; i < 6; i++)
	{
		if (dot(planes[i].xyz, sphere.xyz) + planes[i].w < -sphere.w)
			return false;
	}

	return true;
}

bool ProjectSphereBounds(vec4 sphere, out vec2 minUV, out vec2 maxUV, out float nearestDeviceZ)
{
	vec3 center = sphere.xyz;
	float radius = max(sphere.w * u_PushConstants.BoundsScale, 0.0001);

	minUV = vec2(1.0);
	maxUV = vec2(0.0);
	nearestDeviceZ = 0.0;

	for (uint corner = 0u; corner < 8u; corner++)
	{
		vec3 offset = vec3(
			(corner & 1u) == 0u ? -radius : radius,
			(corner & 2u) == 0u ? -radius : radius,
			(corner & 4u) == 0u ? -radius : radius);

		vec4 clip = u_PushConstants.ViewProjection * vec4(center + offset, 1.0);
		if (clip.w <= 0.0001)
			return false;

		vec3 ndc = clip.xyz / clip.w;
		vec2 uv = ndc.xy * 0.5 + 0.5;
		minUV = min(minUV, uv);
		maxUV = max(maxUV, uv);
		nearestDeviceZ = max(nearestDeviceZ, ndc.z);
	}

	if (maxUV.x <= 0.0 || maxUV.y <= 0.0 || minUV.x >= 1.0 || minUV.y >= 1.0)
		return false;

	minUV = clamp(minUV, vec2(0.0), vec2(1.0));
	maxUV = clamp(maxUV, vec2(0.0), vec2(1.0));
	return maxUV.x > minUV.x && maxUV.y > minUV.y;
}

bool IsSphereOccluded(vec4 sphere)
{
	if (u_PushConstants.OcclusionCullingEnabled == 0 || u_PushConstants.NumDepthMips == 0)
		return false;

	vec2 minUV;
	vec2 maxUV;
	float nearestDeviceZ;
	if (!ProjectSphereBounds(sphere, minUV, maxUV, nearestDeviceZ))
		return false;

	vec2 viewportSize = max(u_PushConstants.HZBUVFactorAndViewportSize.zw, vec2(1.0));
	vec2 rectSize = max((maxUV - minUV) * viewportSize, vec2(1.0));
	float mip = clamp(ceil(log2(max(rectSize.x, rectSize.y))), 0.0, float(u_PushConstants.NumDepthMips - 1u));

	vec2 hzbUVFactor = u_PushConstants.HZBUVFactorAndViewportSize.xy;
	vec2 hzbMinUV = minUV * hzbUVFactor;
	vec2 hzbMaxUV = maxUV * hzbUVFactor;
	vec2 hzbCenterUV = (hzbMinUV + hzbMaxUV) * 0.5;

	float closestDeviceZ = 0.0;
	closestDeviceZ = max(closestDeviceZ, textureLod(sampler2D(u_HZB, r_PointSampler), hzbMinUV, mip).r);
	closestDeviceZ = max(closestDeviceZ, textureLod(sampler2D(u_HZB, r_PointSampler), vec2(hzbMaxUV.x, hzbMinUV.y), mip).r);
	closestDeviceZ = max(closestDeviceZ, textureLod(sampler2D(u_HZB, r_PointSampler), vec2(hzbMinUV.x, hzbMaxUV.y), mip).r);
	closestDeviceZ = max(closestDeviceZ, textureLod(sampler2D(u_HZB, r_PointSampler), hzbMaxUV, mip).r);
	closestDeviceZ = max(closestDeviceZ, textureLod(sampler2D(u_HZB, r_PointSampler), hzbCenterUV, mip).r);

	return closestDeviceZ > nearestDeviceZ + u_PushConstants.DepthBias;
}

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main()
{
	uint drawIndex = gl_WorkGroupID.x;
	if (drawIndex >= u_PushConstants.DrawCount)
		return;

	uvec4 draw = r_MeshCullDrawData.Draws[drawIndex];
	uint objectIndexBase = draw.x;
	uint instanceCount = draw.y;
	uint visibleObjectIndexBase = draw.z;
	uint indirectArgsBase = drawIndex * 5u;

	if (gl_LocalInvocationIndex == 0)
		b_IndirectDrawCommands.Data[indirectArgsBase + 1u] = 0u;

	barrier();

	for (uint instanceIndex = gl_LocalInvocationIndex; instanceIndex < instanceCount; instanceIndex += gl_WorkGroupSize.x)
	{
		uint objectIndex = r_ObjectIndexes.Indices[objectIndexBase + instanceIndex];
		vec4 boundsSphere = r_GPUSceneInstances.Instances[objectIndex].BoundsSphere;

		if (IsSphereVisible(boundsSphere) && !IsSphereOccluded(boundsSphere))
		{
			uint visibleOffset = atomicAdd(b_IndirectDrawCommands.Data[indirectArgsBase + 1u], 1u);
			o_VisibleObjectIndexes.Indices[visibleObjectIndexBase + visibleOffset] = objectIndex;
		}
	}
}
