#version 450 core
#pragma stage : comp

layout(std430, set = 1, binding = 0) readonly buffer MeshCullDrawData
{
	uvec4 Draws[];
} r_MeshCullDrawData;

layout(std430, set = 1, binding = 1) readonly buffer ObjectIndexes
{
	uint Indices[];
} r_ObjectIndexes;

layout(std430, set = 1, binding = 2) readonly buffer InstanceBounds
{
	vec4 Spheres[];
} r_InstanceBounds;

layout(std430, set = 1, binding = 3) writeonly buffer VisibleObjectIndexes
{
	uint Indices[];
} o_VisibleObjectIndexes;

layout(std430, set = 1, binding = 4) buffer IndirectDrawCommands
{
	uint Data[];
} b_IndirectDrawCommands;

layout(push_constant) uniform PushConstants
{
	mat4 ViewProjection;
	uint DrawCount;
	uint CullingEnabled;
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
	if (u_PushConstants.CullingEnabled == 0)
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
		uint transformIndex = objectIndex / 3u;
		vec4 boundsSphere = r_InstanceBounds.Spheres[transformIndex];

		if (IsSphereVisible(boundsSphere))
		{
			uint visibleOffset = atomicAdd(b_IndirectDrawCommands.Data[indirectArgsBase + 1u], 1u);
			o_VisibleObjectIndexes.Indices[visibleObjectIndexBase + visibleOffset] = objectIndex;
		}
	}
}
