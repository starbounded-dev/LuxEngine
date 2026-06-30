// -------------------------------------------------
// -- Lux clustered deferred: cluster AABB build  --
// -------------------------------------------------
//
// One invocation per froxel. Builds the per-cluster view-space AABB grid that
// the cluster light-assignment pass tests lights against. Depends only on the
// camera projection, so it only needs to rerun on resize / projection change.
//
// References:
// - DOOM 2016 clustered shading (id Tech 6)
// - Olsson & Assarsson, "Clustered Deferred and Forward Shading" (2012)
//
#version 450 core
#pragma stage : comp
#include <Cluster.glslh>

// Camera UBO declared inline (matching Buffers.glslh set=2 binding=0) so this
// pass does not pull in the full lighting/scene binding set it does not use.
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
} u_Camera;

layout(std430, set = 1, binding = 20) writeonly buffer ClusterAABBBuffer
{
	ClusterAABB Clusters[];
} s_Clusters;

layout(push_constant) uniform PushConstants
{
	vec4  ScreenSizeNearFar; // xy = render resolution (px), z = zNear, w = zFar
	uvec4 GridSize;          // xyz = cluster grid dims
} u_Push;

// Unproject a clip-space position into view space.
vec3 ClipToView(vec4 clip)
{
	vec4 view = u_Camera.InverseProjectionMatrix * clip;
	return view.xyz / view.w;
}

// Unproject a screen-pixel coordinate into view space (clip z arbitrary; we
// re-intersect the resulting eye ray with the slice planes below).
vec3 ScreenToView(vec2 screenPx, vec2 screenSize)
{
	vec2 uv = screenPx / screenSize;
	vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
	return ClipToView(clip);
}

// Intersect the eye->target ray with a constant-Z view-space plane.
vec3 LineIntersectZPlane(vec3 eye, vec3 target, float z)
{
	vec3 dir = target - eye;
	float t = (z - eye.z) / dir.z;
	return eye + t * dir;
}

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main()
{
	uint clusterIndex = gl_GlobalInvocationID.x;
	if (clusterIndex >= CLUSTER_COUNT)
		return;

	uvec3 grid = u_Push.GridSize.xyz;
	uvec3 c;
	c.x = clusterIndex % grid.x;
	c.y = (clusterIndex / grid.x) % grid.y;
	c.z = clusterIndex / (grid.x * grid.y);

	vec2  screenSize = u_Push.ScreenSizeNearFar.xy;
	float zNear = u_Push.ScreenSizeNearFar.z;
	float zFar  = u_Push.ScreenSizeNearFar.w;

	vec2 tileSize = screenSize / vec2(grid.xy);
	vec2 minPx = vec2(c.xy) * tileSize;
	vec2 maxPx = vec2(c.xy + uvec2(1)) * tileSize;

	// Tile corner rays through the eye (view space).
	vec3 minView = ScreenToView(minPx, screenSize);
	vec3 maxView = ScreenToView(maxPx, screenSize);

	// Exponential near/far view-Z for this slice (negative: camera looks -Z).
	float tileNear = -zNear * pow(zFar / zNear, float(c.z)      / float(grid.z));
	float tileFar  = -zNear * pow(zFar / zNear, float(c.z + 1u) / float(grid.z));

	const vec3 eye = vec3(0.0);
	vec3 minNear = LineIntersectZPlane(eye, minView, tileNear);
	vec3 minFar  = LineIntersectZPlane(eye, minView, tileFar);
	vec3 maxNear = LineIntersectZPlane(eye, maxView, tileNear);
	vec3 maxFar  = LineIntersectZPlane(eye, maxView, tileFar);

	vec3 aabbMin = min(min(minNear, minFar), min(maxNear, maxFar));
	vec3 aabbMax = max(max(minNear, minFar), max(maxNear, maxFar));

	s_Clusters.Clusters[clusterIndex].MinPoint = vec4(aabbMin, 0.0);
	s_Clusters.Clusters[clusterIndex].MaxPoint = vec4(aabbMax, 0.0);
}
