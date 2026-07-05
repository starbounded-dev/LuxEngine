// Pre-depth, mesh-shader path (VK_EXT_mesh_shader).
//
// The task stage culls meshlets (frustum + backface cone) and the mesh stage
// expands the survivors into triangles. Replaces vertex-pipeline draws for
// static meshes when "Mesh Shaders" is enabled in the renderer options.
//
// Set 0 is the per-mesh meshlet data (bound by MeshSource::RT_GetOrCreateMeshletBindingSet):
//   binding 0 = meshlet descriptors, 1 = meshlet vertex indices,
//   binding 2 = packed meshlet triangles, 3 = the shared render vertex buffer.

#version 460 core
#pragma stage : task

#extension GL_EXT_mesh_shader : require

#include <Buffers.glslh>

struct MeshletData
{
	vec4 BoundsSphere; // xyz = mesh-local center, w = radius
	vec4 Cone;         // xyz = axis, w = cutoff (>= 1 disables the cone test)
	uvec4 Counts;      // x = vertex offset, y = triangle offset, z = vertex count, w = triangle count
};

layout(std430, set = 0, binding = 0) readonly buffer Meshlets
{
	MeshletData Data[];
} r_Meshlets;

layout(push_constant) uniform PushConstants
{
	uint ObjectIndexBase;
	uint MeshletOffset;
	uint MeshletCount;
	uint _pad0;
} u_PushConstants;

// One task workgroup culls up to 32 meshlets of one instance.
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

struct TaskPayload
{
	uint MeshletIndices[32];
	uint ObjectIndex;
};
taskPayloadSharedEXT TaskPayload o_Payload;

shared uint s_VisibleCount;

bool IsSphereVisible(vec3 center, float radius)
{
	// Gribb-Hartmann plane extraction from the world -> clip matrix.
	mat4 m = u_Camera.ViewProjectionMatrix;
	vec4 row0 = vec4(m[0][0], m[1][0], m[2][0], m[3][0]);
	vec4 row1 = vec4(m[0][1], m[1][1], m[2][1], m[3][1]);
	vec4 row3 = vec4(m[0][3], m[1][3], m[2][3], m[3][3]);
	vec4 row2 = vec4(m[0][2], m[1][2], m[2][2], m[3][2]);

	vec4 position = vec4(center, 1.0);
	if (dot(row3 + row0, position) < -radius) return false; // left
	if (dot(row3 - row0, position) < -radius) return false; // right
	if (dot(row3 + row1, position) < -radius) return false; // bottom
	if (dot(row3 - row1, position) < -radius) return false; // top
	if (dot(row3 - row2, position) < -radius) return false; // far
	if (dot(row2, position) < -radius) return false;        // near (reverse-Z)
	return true;
}

void main()
{
	if (gl_LocalInvocationIndex == 0)
		s_VisibleCount = 0;
	barrier();

	const uint meshletLocal = gl_WorkGroupID.x * 32u + gl_LocalInvocationIndex;
	const uint objectIndex = u_PushConstants.ObjectIndexBase + gl_WorkGroupID.y;

	if (meshletLocal < u_PushConstants.MeshletCount)
	{
		const MeshletData meshlet = r_Meshlets.Data[u_PushConstants.MeshletOffset + meshletLocal];
		const mat4 transform = GetInstanceTransform(objectIndex);

		// World-space bounds; radius scaled by the largest basis-vector length so
		// non-uniform scale stays conservative for the sphere test.
		const vec3 worldCenter = vec3(transform * vec4(meshlet.BoundsSphere.xyz, 1.0));
		const float scale = max(length(transform[0].xyz), max(length(transform[1].xyz), length(transform[2].xyz)));
		const float worldRadius = meshlet.BoundsSphere.w * scale;

		bool visible = IsSphereVisible(worldCenter, worldRadius);

		// Backface cone (meshoptimizer-style test). Disabled when cutoff >= 1.
		if (visible && meshlet.Cone.w < 1.0)
		{
			const vec3 coneAxis = normalize(mat3(transform) * meshlet.Cone.xyz);
			const vec3 cameraPosition = u_Camera.InverseViewMatrix[3].xyz;
			const vec3 toCenter = worldCenter - cameraPosition;
			if (dot(toCenter, coneAxis) >= meshlet.Cone.w * length(toCenter) + worldRadius)
				visible = false;
		}

		if (visible)
		{
			const uint slot = atomicAdd(s_VisibleCount, 1u);
			o_Payload.MeshletIndices[slot] = u_PushConstants.MeshletOffset + meshletLocal;
		}
	}

	if (gl_LocalInvocationIndex == 0)
		o_Payload.ObjectIndex = objectIndex;

	barrier();
	EmitMeshTasksEXT(s_VisibleCount, 1, 1);
}

#version 460 core
#pragma stage : mesh

#extension GL_EXT_mesh_shader : require

#include <Buffers.glslh>

struct MeshletData
{
	vec4 BoundsSphere;
	vec4 Cone;
	uvec4 Counts; // x = vertex offset, y = triangle offset, z = vertex count, w = triangle count
};

layout(std430, set = 0, binding = 0) readonly buffer Meshlets
{
	MeshletData Data[];
} r_Meshlets;

layout(std430, set = 0, binding = 1) readonly buffer MeshletVertexIndices
{
	uint Data[];
} r_MeshletVertices;

// One packed uint per triangle: 3 x 8-bit meshlet-local vertex slots.
layout(std430, set = 0, binding = 2) readonly buffer MeshletTriangles
{
	uint Data[];
} r_MeshletTriangles;

// The shared render vertex buffer viewed as raw floats
// (14 floats per vertex: position 3, normal 3, tangent 3, binormal 3, uv 2).
layout(std430, set = 0, binding = 3) readonly buffer MeshVertexData
{
	float Data[];
} r_VertexData;

#define MESH_VERTEX_STRIDE 14u

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

// The GBuffer pass depth-tests with Equal against this pass's output, so the
// position math must match the vertex-pipeline shaders bit-for-bit: same
// two-step expression as GBuffer_Static/PreDepth, with gl_Position invariant.
out gl_MeshPerVertexEXT
{
	invariant vec4 gl_Position;
} gl_MeshVerticesEXT[];

struct TaskPayload
{
	uint MeshletIndices[32];
	uint ObjectIndex;
};
taskPayloadSharedEXT TaskPayload i_Payload;

void main()
{
	const MeshletData meshlet = r_Meshlets.Data[i_Payload.MeshletIndices[gl_WorkGroupID.x]];
	const uint vertexCount = meshlet.Counts.z;
	const uint triangleCount = meshlet.Counts.w;

	SetMeshOutputsEXT(vertexCount, triangleCount);

	const mat4 transform = GetInstanceTransform(i_Payload.ObjectIndex);

	for (uint i = gl_LocalInvocationIndex; i < vertexCount; i += 64u)
	{
		const uint vertexIndex = r_MeshletVertices.Data[meshlet.Counts.x + i];
		const uint base = vertexIndex * MESH_VERTEX_STRIDE;
		const vec3 position = vec3(r_VertexData.Data[base + 0u], r_VertexData.Data[base + 1u], r_VertexData.Data[base + 2u]);

		vec4 worldPosition = transform * vec4(position, 1.0);
		gl_MeshVerticesEXT[i].gl_Position = u_Camera.ViewProjectionMatrix * worldPosition;
	}

	for (uint t = gl_LocalInvocationIndex; t < triangleCount; t += 64u)
	{
		const uint packed = r_MeshletTriangles.Data[meshlet.Counts.y + t];
		gl_PrimitiveTriangleIndicesEXT[t] = uvec3(packed & 0xFFu, (packed >> 8u) & 0xFFu, (packed >> 16u) & 0xFFu);
	}
}

#version 460 core
#pragma stage : frag

void main()
{
}
