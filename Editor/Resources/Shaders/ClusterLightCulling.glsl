// -------------------------------------------------
// -- Lux clustered deferred: light assignment     --
// -------------------------------------------------
//
// One invocation per froxel. Culls point and spot lights against the cluster's
// view-space AABB (built by ClusterBuild) and writes, per light type:
//   * a packed global index list (dynamically allocated via an atomic counter)
//   * a per-cluster grid entry (offset, count) into that list
//
// Point and spot lights use independent (parallel) grids + lists. The lists are
// dynamically packed — no fixed per-cluster cap — so memory tracks actual light
// overlap rather than numClusters * MaxLightsPerCluster.
//
// References:
// - DOOM 2016 clustered shading (id Tech 6)
// - Olsson & Assarsson, "Clustered Deferred and Forward Shading" (2012)
//
#version 450 core
#pragma stage : comp
#include <Buffers.glslh>   // u_Camera (set 2), u_PointLights / u_SpotLights (set 1)
#include <Cluster.glslh>

layout(std430, set = 1, binding = 20) readonly buffer ClusterAABBBuffer
{
	ClusterAABB Clusters[];
} s_Clusters;

// [0] = point list write cursor, [1] = spot list write cursor. Reset to 0 each
// frame on the host before dispatch (clearBufferUInt).
layout(std430, set = 1, binding = 21) buffer ClusterCounterBuffer
{
	uint Counters[];
} s_Counter;

layout(std430, set = 1, binding = 22) writeonly buffer PointLightGridBuffer
{
	uvec2 Grid[]; // (offset, count)
} s_PointGrid;

layout(std430, set = 1, binding = 23) writeonly buffer SpotLightGridBuffer
{
	uvec2 Grid[];
} s_SpotGrid;

layout(std430, set = 1, binding = 24) writeonly buffer PointLightIndexListBuffer
{
	uint Indices[];
} s_PointList;

layout(std430, set = 1, binding = 25) writeonly buffer SpotLightIndexListBuffer
{
	uint Indices[];
} s_SpotList;

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main()
{
	uint cluster = gl_GlobalInvocationID.x;
	if (cluster >= uint(CLUSTER_COUNT))
		return;

	vec3 mn = s_Clusters.Clusters[cluster].MinPoint.xyz;
	vec3 mx = s_Clusters.Clusters[cluster].MaxPoint.xyz;

	mat4 view = u_Camera.ViewMatrix;

	// ── Point lights ─────────────────────────────────────────────────────────
	uint pointCount = min(u_PointLights.LightCount, 1000u);
	uint visiblePoints = 0u;
	for (uint i = 0u; i < pointCount; ++i)
	{
		vec3 posVS = (view * vec4(u_PointLights.Lights[i].Position, 1.0)).xyz;
		if (ClusterSphereIntersectsAABB(posVS, u_PointLights.Lights[i].Radius, mn, mx))
			++visiblePoints;
	}

	uint pointOffset = atomicAdd(s_Counter.Counters[0], visiblePoints);
	uint pointCapacity = (pointOffset >= CLUSTER_MAX_POINT_INDICES)
		? 0u : min(visiblePoints, CLUSTER_MAX_POINT_INDICES - pointOffset);

	uint pointWritten = 0u;
	for (uint i = 0u; i < pointCount && pointWritten < pointCapacity; ++i)
	{
		vec3 posVS = (view * vec4(u_PointLights.Lights[i].Position, 1.0)).xyz;
		if (ClusterSphereIntersectsAABB(posVS, u_PointLights.Lights[i].Radius, mn, mx))
			s_PointList.Indices[pointOffset + pointWritten++] = i;
	}
	s_PointGrid.Grid[cluster] = uvec2(pointOffset, pointWritten);

	// ── Spot lights ──────────────────────────────────────────────────────────
	uint spotCount = min(u_SpotLights.LightCount, 1024u);
	uint visibleSpots = 0u;
	for (uint i = 0u; i < spotCount; ++i)
	{
		vec3 posVS = (view * vec4(u_SpotLights.Lights[i].Position, 1.0)).xyz;
		if (ClusterSphereIntersectsAABB(posVS, u_SpotLights.Lights[i].Range, mn, mx))
			++visibleSpots;
	}

	uint spotOffset = atomicAdd(s_Counter.Counters[1], visibleSpots);
	uint spotCapacity = (spotOffset >= CLUSTER_MAX_SPOT_INDICES)
		? 0u : min(visibleSpots, CLUSTER_MAX_SPOT_INDICES - spotOffset);

	uint spotWritten = 0u;
	for (uint i = 0u; i < spotCount && spotWritten < spotCapacity; ++i)
	{
		vec3 posVS = (view * vec4(u_SpotLights.Lights[i].Position, 1.0)).xyz;
		if (ClusterSphereIntersectsAABB(posVS, u_SpotLights.Lights[i].Range, mn, mx))
			s_SpotList.Indices[spotOffset + spotWritten++] = i;
	}
	s_SpotGrid.Grid[cluster] = uvec2(spotOffset, spotWritten);
}
