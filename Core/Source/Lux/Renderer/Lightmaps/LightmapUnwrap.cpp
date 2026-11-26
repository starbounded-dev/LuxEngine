// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "LightmapUnwrap.h"

#include <xatlas/xatlas.h>

namespace Lux {

	bool LightmapUVsOverlap(const MeshSource& source, const Submesh& submesh)
	{
		constexpr int k_Grid = 128;
		const auto& vertices = source.GetVertices();
		const auto& attributes = source.GetVertexAttributes();
		const auto& indices = source.GetIndices();
		std::vector<uint32_t> owner(static_cast<size_t>(k_Grid) * k_Grid, 0u);
		uint32_t covered = 0, shared = 0;
		for (uint32_t t = 0; t < submesh.IndexCount / 3; t++)
		{
			glm::vec2 uv[3];
			for (uint32_t corner = 0; corner < 3; corner++)
			{
				const uint32_t scalar = submesh.BaseIndex + t * 3 + corner;
				if (scalar / 3 >= indices.size())
					return false;
				const Index& index = indices[scalar / 3];
				const uint32_t local = (scalar % 3) == 0 ? index.V1 : ((scalar % 3) == 1 ? index.V2 : index.V3);
				const uint32_t vertex = submesh.BaseVertex + local;
				if (vertex >= vertices.size())
					return false;
				uv[corner] = (vertex < attributes.size() ? glm::unpackHalf2x16(attributes[vertex].UV1) : vertices[vertex].Texcoord) * static_cast<float>(k_Grid);
			}
			const float denominator = (uv[1].y - uv[2].y) * (uv[0].x - uv[2].x) + (uv[2].x - uv[1].x) * (uv[0].y - uv[2].y);
			if (std::abs(denominator) < 1e-9f)
				continue;
			const glm::vec2 lo = glm::min(uv[0], glm::min(uv[1], uv[2])), hi = glm::max(uv[0], glm::max(uv[1], uv[2]));
			for (int y = std::max(0, static_cast<int>(std::floor(lo.y))); y <= std::min(k_Grid - 1, static_cast<int>(std::ceil(hi.y))); y++)
			{
				for (int x = std::max(0, static_cast<int>(std::floor(lo.x))); x <= std::min(k_Grid - 1, static_cast<int>(std::ceil(hi.x))); x++)
				{
					const glm::vec2 p(x + 0.5f, y + 0.5f);
					const float w0 = ((uv[1].y - uv[2].y) * (p.x - uv[2].x) + (uv[2].x - uv[1].x) * (p.y - uv[2].y)) / denominator;
					const float w1 = ((uv[2].y - uv[0].y) * (p.x - uv[2].x) + (uv[0].x - uv[2].x) * (p.y - uv[2].y)) / denominator;
					if (w0 < 1e-4f || w1 < 1e-4f || 1.0f - w0 - w1 < 1e-4f)
						continue; // strictly inside: triangles that only share an edge do not overlap
					uint32_t& cell = owner[static_cast<size_t>(y) * k_Grid + x];
					if (cell == 0u)
					{
						cell = t + 1u;
						covered++;
					}
					else if (cell != t + 1u)
						shared++;
				}
			}
		}
		return shared > 16 && shared * 20 > covered;
	}

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
