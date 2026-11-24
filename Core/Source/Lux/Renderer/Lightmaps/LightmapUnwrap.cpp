// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "LightmapUnwrap.h"

#include <xatlas/xatlas.h>

namespace Lux {

	LightmapUnwrapResult UnwrapSubmeshForLightmap(const MeshSource& source, const Submesh& submesh, uint32_t resolution, uint32_t padding)
	{
		LightmapUnwrapResult result;
		const auto& vertices = source.GetVertices();
		const auto& indices = source.GetIndices();
		if (submesh.VertexCount == 0 || submesh.IndexCount < 3)
			return result;

		std::vector<glm::vec3> positions(submesh.VertexCount);
		std::vector<glm::vec3> normals(submesh.VertexCount);
		for (uint32_t v = 0; v < submesh.VertexCount; v++)
		{
			const uint32_t index = submesh.BaseVertex + v;
			if (index >= vertices.size())
				return result;
			positions[v] = vertices[index].Position;
			normals[v] = vertices[index].Normal;
		}
		std::vector<uint32_t> triangleIndices(submesh.IndexCount);
		for (uint32_t i = 0; i < submesh.IndexCount; i++)
		{
			const uint32_t scalar = submesh.BaseIndex + i;
			const uint32_t triangle = scalar / 3;
			if (triangle >= indices.size())
				return result;
			const Index& index = indices[triangle];
			const uint32_t local = (scalar % 3) == 0 ? index.V1 : ((scalar % 3) == 1 ? index.V2 : index.V3);
			if (local >= submesh.VertexCount)
				return result;
			triangleIndices[i] = local;
		}

		xatlas::Atlas* atlas = xatlas::Create();
		xatlas::MeshDecl declaration;
		declaration.vertexPositionData = positions.data();
		declaration.vertexPositionStride = sizeof(glm::vec3);
		declaration.vertexNormalData = normals.data();
		declaration.vertexNormalStride = sizeof(glm::vec3);
		declaration.vertexCount = submesh.VertexCount;
		declaration.indexData = triangleIndices.data();
		declaration.indexCount = submesh.IndexCount;
		declaration.indexFormat = xatlas::IndexFormat::UInt32;
		if (xatlas::AddMesh(atlas, declaration) != xatlas::AddMeshError::Success)
		{
			xatlas::Destroy(atlas);
			return result;
		}
		xatlas::ChartOptions chartOptions;
		xatlas::PackOptions packOptions;
		packOptions.resolution = resolution;
		packOptions.padding = padding;
		packOptions.bruteForce = false;
		xatlas::Generate(atlas, chartOptions, packOptions);

		if (atlas->meshCount == 1 && atlas->width > 0 && atlas->height > 0)
		{
			const xatlas::Mesh& mesh = atlas->meshes[0];
			result.SourceVertex.resize(mesh.vertexCount);
			result.UV.resize(mesh.vertexCount);
			for (uint32_t i = 0; i < mesh.vertexCount; i++)
			{
				result.SourceVertex[i] = mesh.vertexArray[i].xref;
				result.UV[i] = glm::vec2(mesh.vertexArray[i].uv[0] / static_cast<float>(atlas->width), mesh.vertexArray[i].uv[1] / static_cast<float>(atlas->height));
			}
			result.Indices.assign(mesh.indexArray, mesh.indexArray + mesh.indexCount);
			result.ChartCount = mesh.chartCount;
			result.AtlasSize = std::max(atlas->width, atlas->height);
			result.Utilization = atlas->utilization ? atlas->utilization[0] : 0.0f;
			result.Success = true;
		}
		xatlas::Destroy(atlas);
		return result;
	}

}
