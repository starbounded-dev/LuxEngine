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

// Lights are walked in chunks of 64: each invocation transforms one light of the chunk to view space
// into shared memory, then all test the whole chunk, so a light is transformed once per workgroup
// instead of once per froxel.
#define LIGHT_CHUNK 64u
shared vec4 s_Lights[LIGHT_CHUNK]; // view-space position, range

const uint PointLights = 0u;
const uint SpotLights = 1u;

vec4 LoadViewSpaceLight(uint type, uint index)
{
	vec3 position = type == PointLights ? u_PointLights.Lights[index].Position : u_SpotLights.Lights[index].Position;
	float range = type == PointLights ? u_PointLights.Lights[index].Radius : u_SpotLights.Lights[index].Range;
	return vec4((u_Camera.ViewMatrix * vec4(position, 1.0)).xyz, range);
}

// Counts the lights of `type` touching the froxel and, with `write`, stores the first `capacity` of them
// at `offset` in that type's list. Every invocation runs it (the barriers need uniform control flow);
// `valid` is false for invocations past the grid.
uint AssignLights(uint type, uint count, bool write, uint offset, uint capacity, vec3 mn, vec3 mx, bool valid)
{
	uint hits = 0u;
	for (uint base = 0u; base < count; base += LIGHT_CHUNK)
	{
		memoryBarrierShared();
		barrier(); // the previous chunk is read by everyone
		uint index = base + gl_LocalInvocationIndex;
		if (index < count)
			s_Lights[gl_LocalInvocationIndex] = LoadViewSpaceLight(type, index);
		memoryBarrierShared();
		barrier();

		uint chunk = min(LIGHT_CHUNK, count - base);
		for (uint j = 0u; j < chunk; ++j)
		{
			vec4 light = s_Lights[j];
			if (!valid || !ClusterSphereIntersectsAABB(light.xyz, light.w, mn, mx))
				continue;
			if (write && hits < capacity)
			{
				if (type == PointLights)
					s_PointList.Indices[offset + hits] = base + j;
				else
					s_SpotList.Indices[offset + hits] = base + j;
			}
			++hits;
		}
	}
	return hits;
}

void main()
{
	uint cluster = gl_GlobalInvocationID.x;
	bool valid = cluster < uint(CLUSTER_COUNT);
	uint clamped = min(cluster, uint(CLUSTER_COUNT) - 1u);
	vec3 mn = s_Clusters.Clusters[clamped].MinPoint.xyz;
	vec3 mx = s_Clusters.Clusters[clamped].MaxPoint.xyz;

	// Counters[0]/[1] end as the total entries every froxel asked for; past the list capacity the
	// renderer reports the overflow (SceneRenderer::Statistics::ClusterPointLightsDropped).
	uint pointCount = min(u_PointLights.LightCount, 1000u);
	uint visiblePoints = AssignLights(PointLights, pointCount, false, 0u, 0u, mn, mx, valid);
	uint pointOffset = valid ? atomicAdd(s_Counter.Counters[0], visiblePoints) : 0u;
	uint pointCapacity = (pointOffset >= CLUSTER_MAX_POINT_INDICES)
		? 0u : min(visiblePoints, CLUSTER_MAX_POINT_INDICES - pointOffset);
	uint pointWritten = min(AssignLights(PointLights, pointCount, true, pointOffset, pointCapacity, mn, mx, valid), pointCapacity);
	if (valid)
		s_PointGrid.Grid[cluster] = uvec2(pointOffset, pointWritten);

	uint spotCount = min(u_SpotLights.LightCount, 1024u);
	uint visibleSpots = AssignLights(SpotLights, spotCount, false, 0u, 0u, mn, mx, valid);
	uint spotOffset = valid ? atomicAdd(s_Counter.Counters[1], visibleSpots) : 0u;
	uint spotCapacity = (spotOffset >= CLUSTER_MAX_SPOT_INDICES)
		? 0u : min(visibleSpots, CLUSTER_MAX_SPOT_INDICES - spotOffset);
	uint spotWritten = min(AssignLights(SpotLights, spotCount, true, spotOffset, spotCapacity, mn, mx, valid), spotCapacity);
	if (valid)
		s_SpotGrid.Grid[cluster] = uvec2(spotOffset, spotWritten);
}
