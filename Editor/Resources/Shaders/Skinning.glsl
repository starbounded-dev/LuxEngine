// Skins a mesh's vertices on the GPU: one invocation per vertex. Each skinned entity gets its own copy of
// the mesh's vertex buffer, rewritten here every frame, and every raster pass then draws that copy like
// an ordinary static mesh (so the depth, G-buffer, shadow and selection pipelines are unchanged).
//
// Linear blend skinning: the vertex is moved by the weight-blended sum of up to four palette matrices
// (bone model pose * inverse bind pose, Animation/SkinPalette.h) and its tangent frame is rotated by
// the same blend. The frame goes through the blended matrix's 3x3, which is exact for rotation and
// uniform scale (a non-uniform bone scale skews normals slightly). A vertex no bone touches (weights
// that add up to zero) is copied unchanged.

#version 450 core
#pragma stage : comp

#include <QTangent.glslh>

layout(local_size_x = 64) in;

// The mesh's vertices as raw words: PackedVertex (Mesh.h), 7 words each: position (3 floats), the QTangent
// (four snorm16 in 2 words), texcoord (2 floats).
layout(std430, set = 1, binding = 40) readonly buffer SkinSourceVertices
{
	uint Words[];
} r_SkinSource;

// BoneInfluence (Mesh.h), 8 words per vertex: four palette indices, then four float weights.
layout(std430, set = 1, binding = 41) readonly buffer SkinInfluences
{
	uint Words[];
} r_SkinInfluences;

// Every skinned entity's palette, back to back; an entity's starts at PaletteBase.
layout(std430, set = 1, binding = 42) readonly buffer SkinPalette
{
	mat4 Matrices[];
} r_SkinPalette;

// Every skinned entity's copy, back to back: an entity's vertex v lands at OutputBase + v (same layout
// as the source). A draw adds OutputBase to its base vertex.
layout(std430, set = 1, binding = 43) writeonly buffer SkinnedVertices
{
	uint Words[];
} b_SkinOutput;

layout(push_constant) uniform SkinPushConstants
{
	uint VertexCount;
	uint PaletteBase;
	uint OutputBase;
	uint _pad0;
} u_Skin;

const uint SKIN_VERTEX_WORDS = 7u;
const uint SKIN_INFLUENCE_WORDS = 8u;
const float QTANGENT_MIN_W = 1.0 / 32767.0;

// The inverse of QTangent_ToTBN: the unit quaternion of a right-handed frame (columns tangent,
// binormal, normal), with the binormal's handedness in the sign of w. w is kept off zero so that sign
// survives snorm16 (PackedVertex::PackedVertex does the same on the CPU).
vec4 QTangent_FromFrame(mat3 m, float handedness)
{
	vec4 q;
	float trace = m[0][0] + m[1][1] + m[2][2];
	if (trace > 0.0)
	{
		float s = sqrt(trace + 1.0) * 2.0;
		q = vec4((m[1][2] - m[2][1]) / s, (m[2][0] - m[0][2]) / s, (m[0][1] - m[1][0]) / s, 0.25 * s);
	}
	else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
	{
		float s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
		q = vec4(0.25 * s, (m[1][0] + m[0][1]) / s, (m[2][0] + m[0][2]) / s, (m[1][2] - m[2][1]) / s);
	}
	else if (m[1][1] > m[2][2])
	{
		float s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
		q = vec4((m[1][0] + m[0][1]) / s, 0.25 * s, (m[2][1] + m[1][2]) / s, (m[2][0] - m[0][2]) / s);
	}
	else
	{
		float s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
		q = vec4((m[2][0] + m[0][2]) / s, (m[2][1] + m[1][2]) / s, 0.25 * s, (m[0][1] - m[1][0]) / s);
	}

	q = normalize(q);
	if (q.w < 0.0)
		q = -q;
	if (q.w < QTANGENT_MIN_W)
	{
		q.w = QTANGENT_MIN_W;
		q = normalize(q);
	}
	return handedness < 0.0 ? -q : q;
}

void main()
{
	uint vertex = gl_GlobalInvocationID.x;
	if (vertex >= u_Skin.VertexCount)
		return;

	uint source = vertex * SKIN_VERTEX_WORDS;
	uint influence = vertex * SKIN_INFLUENCE_WORDS;
	uint target = (u_Skin.OutputBase + vertex) * SKIN_VERTEX_WORDS;

	uvec4 indices = uvec4(r_SkinInfluences.Words[influence], r_SkinInfluences.Words[influence + 1u],
		r_SkinInfluences.Words[influence + 2u], r_SkinInfluences.Words[influence + 3u]);
	vec4 weights = vec4(uintBitsToFloat(r_SkinInfluences.Words[influence + 4u]), uintBitsToFloat(r_SkinInfluences.Words[influence + 5u]),
		uintBitsToFloat(r_SkinInfluences.Words[influence + 6u]), uintBitsToFloat(r_SkinInfluences.Words[influence + 7u]));

	uint out0 = r_SkinSource.Words[source];
	uint out1 = r_SkinSource.Words[source + 1u];
	uint out2 = r_SkinSource.Words[source + 2u];
	uint out3 = r_SkinSource.Words[source + 3u];
	uint out4 = r_SkinSource.Words[source + 4u];

	if (weights.x + weights.y + weights.z + weights.w > 1e-6)
	{
		mat4 blended = r_SkinPalette.Matrices[u_Skin.PaletteBase + indices.x] * weights.x
			+ r_SkinPalette.Matrices[u_Skin.PaletteBase + indices.y] * weights.y
			+ r_SkinPalette.Matrices[u_Skin.PaletteBase + indices.z] * weights.z
			+ r_SkinPalette.Matrices[u_Skin.PaletteBase + indices.w] * weights.w;

		vec3 position = vec3(uintBitsToFloat(out0), uintBitsToFloat(out1), uintBitsToFloat(out2));
		position = (blended * vec4(position, 1.0)).xyz;

		vec4 q = vec4(unpackSnorm2x16(out3), unpackSnorm2x16(out4));
		float handedness = q.w < 0.0 ? -1.0 : 1.0;
		mat3 frame = QTangent_ToTBN(q); // columns: tangent, binormal, normal
		mat3 linear = mat3(blended);
		vec3 normal = normalize(linear * frame[2]);
		vec3 tangent = linear * frame[0];
		tangent = normalize(tangent - normal * dot(normal, tangent));
		q = QTangent_FromFrame(mat3(tangent, cross(normal, tangent), normal), handedness);

		out0 = floatBitsToUint(position.x);
		out1 = floatBitsToUint(position.y);
		out2 = floatBitsToUint(position.z);
		out3 = packSnorm2x16(q.xy);
		out4 = packSnorm2x16(q.zw);
	}

	b_SkinOutput.Words[target] = out0;
	b_SkinOutput.Words[target + 1u] = out1;
	b_SkinOutput.Words[target + 2u] = out2;
	b_SkinOutput.Words[target + 3u] = out3;
	b_SkinOutput.Words[target + 4u] = out4;
	b_SkinOutput.Words[target + 5u] = r_SkinSource.Words[source + 5u];
	b_SkinOutput.Words[target + 6u] = r_SkinSource.Words[source + 6u];
}
