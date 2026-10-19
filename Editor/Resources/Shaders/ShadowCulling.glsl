#version 450 core
#pragma stage : comp

// GPU shadow culling for SceneRenderer's shadow cull groups (static casters; moving ones take the CPU
// path). Per instance: the LOD (as the main view), each directional cascade (frustum plus the tiny-caster
// cull of SceneRenderer::GetDirectionalShadowCascadeMask) and the spot atlas (any spot tile's frustum, as
// IsSpotShadowCasterVisible). Each visible instance is appended to its (target, LOD) slot: its own
// instance range and indirect args, which the static shadow caches draw.

#include <MeshLOD.glslh>

// The same buffers and slots as MeshCulling.glsl.
// Three uvec4 per shadow cull group, mirroring SceneRenderer's shadow records:
//   [0] x ObjectIndexBase, y InstanceCount, z OutputBase, w IndirectArgsBase
//   [1] as MeshLOD.glslh's lodData
//   [2] unused
// Slot (target, lod) = target * LODCount + lod: its instances at OutputBase + slot * InstanceCount, its
// args at IndirectArgsBase + slot. The frustum planes (six per frustum: four cascades, then the spot
// tiles) start at uvec4 FrustumBase.
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

// Bound to the ObjectIndexes buffer: the shadow passes read their instances from it.
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
	vec4 CameraPositionAndLODScale; // xyz camera position, w MeshLODDistanceScale
	uint DrawCount;
	uint RecordBase;
	uint FrustumBase;
	uint CascadeCount;
	uint SpotCount;
	uint LODEnabled;
	uint ShadowCullingEnabled;
	float TinyCasterPixelsPerRadian;
} u_PushConstants;

const uint ShadowTargetCount = 5u; // four cascades, then the spot atlas
const uint SpotTarget = 4u;

// Frustum::IsSphereVisible with the CPU's own planes.
bool IsSphereInFrustum(uint frustumIndex, vec4 sphere)
{
	for (uint plane = 0u; plane < 6u; plane++)
	{
		vec4 p = uintBitsToFloat(r_MeshCullDrawData.Draws[u_PushConstants.FrustumBase + frustumIndex * 6u + plane]);
		precise float planeDistance = dot(p.xyz, sphere.xyz) + p.w;
		if (planeDistance < -sphere.w)
			return false;
	}
	return true;
}

// SceneRenderer::ShouldCullTinyDirectionalShadowCaster.
bool IsTinyCaster(vec4 sphere, uint cascade)
{
	if (cascade < 2u)
		return false;

	precise float radius = max(sphere.w, 0.001);
	precise float cameraDistance = max(distance(u_PushConstants.CameraPositionAndLODScale.xyz, sphere.xyz) - radius, 0.001);
	precise float projectedRadiusPixels = (radius / cameraDistance) * u_PushConstants.TinyCasterPixelsPerRadian;
	return projectedRadiusPixels < (cascade >= 3u ? 1.25 : 0.75);
}

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main()
{
	uint drawIndex = gl_WorkGroupID.x;
	if (drawIndex >= u_PushConstants.DrawCount)
		return;

	uint record = (u_PushConstants.RecordBase + drawIndex) * 3u;
	uvec4 draw = r_MeshCullDrawData.Draws[record + 0u];
	uvec4 lodData = r_MeshCullDrawData.Draws[record + 1u];
	uint lodCount = clamp(lodData.x & 0xFFu, 1u, 4u);
	uint objectIndexBase = draw.x;
	uint instanceCount = draw.y;
	uint outputBase = draw.z;
	uint argsBase = draw.w;

	uint slotCount = ShadowTargetCount * lodCount;
	for (uint slot = gl_LocalInvocationIndex; slot < slotCount; slot += gl_WorkGroupSize.x)
		b_IndirectDrawCommands.Data[(argsBase + slot) * 5u + 1u] = 0u;

	// As MeshCulling.glsl: order the resets before the atomic appends.
	memoryBarrierBuffer();
	barrier();

	bool lodEnabled = u_PushConstants.LODEnabled != 0u;
	bool cullingEnabled = u_PushConstants.ShadowCullingEnabled != 0u;
	for (uint instanceIndex = gl_LocalInvocationIndex; instanceIndex < instanceCount; instanceIndex += gl_WorkGroupSize.x)
	{
		uint objectIndex = r_ObjectIndexes.Indices[objectIndexBase + instanceIndex];
		vec4 sphere = r_GPUSceneInstances.Instances[objectIndex].BoundsSphere;
		uint lod = SelectMeshLOD(sphere, u_PushConstants.CameraPositionAndLODScale.xyz, u_PushConstants.CameraPositionAndLODScale.w, lodEnabled, lodCount, lodData);

		for (uint cascade = 0u; cascade < u_PushConstants.CascadeCount; cascade++)
		{
			if (cullingEnabled && (!IsSphereInFrustum(cascade, sphere) || IsTinyCaster(sphere, cascade)))
				continue;

			uint slot = cascade * lodCount + lod;
			uint offset = atomicAdd(b_IndirectDrawCommands.Data[(argsBase + slot) * 5u + 1u], 1u);
			o_VisibleObjectIndexes.Indices[outputBase + slot * instanceCount + offset] = objectIndex;
		}

		bool spotVisible = u_PushConstants.SpotCount > 0u && !cullingEnabled;
		for (uint spot = 0u; cullingEnabled && !spotVisible && spot < u_PushConstants.SpotCount; spot++)
			spotVisible = IsSphereInFrustum(SpotTarget + spot, sphere);

		if (spotVisible)
		{
			uint slot = SpotTarget * lodCount + lod;
			uint offset = atomicAdd(b_IndirectDrawCommands.Data[(argsBase + slot) * 5u + 1u], 1u);
			o_VisibleObjectIndexes.Indices[outputBase + slot * instanceCount + offset] = objectIndex;
		}
	}
}
