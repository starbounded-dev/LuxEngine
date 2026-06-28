#include "lpch.h"
#include "MeshFactory.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/MaterialAsset.h"

#include <cmath>
#include <numbers>

namespace Lux
{
	namespace
	{
		AssetHandle GetDefaultPrimitiveMaterial()
		{
			static AssetHandle s_DefaultMaterialHandle = 0;
			if (s_DefaultMaterialHandle && AssetManager::IsAssetHandleValid(s_DefaultMaterialHandle))
				return s_DefaultMaterialHandle;

			Ref<MaterialAsset> materialAsset = Ref<MaterialAsset>::Create(false);
			s_DefaultMaterialHandle = AssetManager::AddMemoryOnlyAsset(materialAsset);
			return s_DefaultMaterialHandle;
		}

		AssetHandle CreatePrimitiveAsset(const std::vector<Vertex>& vertices, const std::vector<Index>& indices)
		{
			const AssetHandle defaultMaterialHandle = GetDefaultPrimitiveMaterial();

			Ref<MeshSource> meshSource = Ref<MeshSource>::Create(vertices, indices, glm::mat4(1.0f));
			meshSource->GetMaterials().push_back(defaultMaterialHandle);
			AssetHandle meshSourceHandle = AssetManager::AddMemoryOnlyAsset(meshSource);

			Ref<StaticMesh> staticMesh = Ref<StaticMesh>::Create(meshSourceHandle, false);
			staticMesh->GetMaterials()->SetMaterial(0, defaultMaterialHandle);
			return AssetManager::AddMemoryOnlyAsset(staticMesh);
		}

		void CalculateCapsuleRing(size_t segments, float ringRadius, float normalY, float bodyOffset, float height, float actualRadius, float v, std::vector<Vertex>& vertices)
		{
			const float segmentStep = 1.0f / (float)(segments - 1);
			for (size_t segment = 0; segment < segments; segment++)
			{
				const float angle = std::numbers::pi_v<float> * 2.0f * (float)segment * segmentStep;
				const float unitX = std::cos(angle) * ringRadius;
				const float unitZ = std::sin(angle) * ringRadius;

				Vertex& vertex = vertices.emplace_back();
				vertex.Position = glm::vec3(actualRadius * unitX, actualRadius * normalY + height * bodyOffset, actualRadius * unitZ);
				vertex.Normal = glm::normalize(glm::vec3(unitX, normalY, unitZ));
				vertex.Texcoord = { (float)segment * segmentStep, v };
			}
		}
	}

	AssetHandle MeshFactory::CreateBox(const glm::vec3& size)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::vector<Vertex> vertices(8);
		vertices[0].Position = { -size.x * 0.5f, -size.y * 0.5f,  size.z * 0.5f };
		vertices[1].Position = {  size.x * 0.5f, -size.y * 0.5f,  size.z * 0.5f };
		vertices[2].Position = {  size.x * 0.5f,  size.y * 0.5f,  size.z * 0.5f };
		vertices[3].Position = { -size.x * 0.5f,  size.y * 0.5f,  size.z * 0.5f };
		vertices[4].Position = { -size.x * 0.5f, -size.y * 0.5f, -size.z * 0.5f };
		vertices[5].Position = {  size.x * 0.5f, -size.y * 0.5f, -size.z * 0.5f };
		vertices[6].Position = {  size.x * 0.5f,  size.y * 0.5f, -size.z * 0.5f };
		vertices[7].Position = { -size.x * 0.5f,  size.y * 0.5f, -size.z * 0.5f };

		for (Vertex& vertex : vertices)
			vertex.Normal = glm::normalize(vertex.Position);

		std::vector<Index> indices = {
			{ 0, 1, 2 }, { 2, 3, 0 },
			{ 1, 5, 6 }, { 6, 2, 1 },
			{ 7, 6, 5 }, { 5, 4, 7 },
			{ 4, 0, 3 }, { 3, 7, 4 },
			{ 4, 5, 1 }, { 1, 0, 4 },
			{ 3, 2, 6 }, { 6, 7, 3 }
		};

		return CreatePrimitiveAsset(vertices, indices);
	}

	AssetHandle MeshFactory::CreateSphere(float radius)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::vector<Vertex> vertices;
		std::vector<Index> indices;

		constexpr uint32_t latitudeBands = 30;
		constexpr uint32_t longitudeBands = 30;

		vertices.reserve((latitudeBands + 1) * (longitudeBands + 1));
		indices.reserve(latitudeBands * longitudeBands * 2);

		for (uint32_t latitude = 0; latitude <= latitudeBands; latitude++)
		{
			const float theta = (float)latitude * std::numbers::pi_v<float> / (float)latitudeBands;
			const float sinTheta = std::sin(theta);
			const float cosTheta = std::cos(theta);

			for (uint32_t longitude = 0; longitude <= longitudeBands; longitude++)
			{
				const float phi = (float)longitude * std::numbers::pi_v<float> * 2.0f / (float)longitudeBands;
				const float sinPhi = std::sin(phi);
				const float cosPhi = std::cos(phi);

				Vertex vertex{};
				vertex.Normal = glm::normalize(glm::vec3(cosPhi * sinTheta, cosTheta, sinPhi * sinTheta));
				vertex.Position = radius * vertex.Normal;
				vertex.Texcoord = { (float)longitude / (float)longitudeBands, (float)latitude / (float)latitudeBands };
				vertices.push_back(vertex);
			}
		}

		for (uint32_t latitude = 0; latitude < latitudeBands; latitude++)
		{
			for (uint32_t longitude = 0; longitude < longitudeBands; longitude++)
			{
				const uint32_t first = latitude * (longitudeBands + 1) + longitude;
				const uint32_t second = first + longitudeBands + 1;

				indices.push_back({ first, second, first + 1 });
				indices.push_back({ second, second + 1, first + 1 });
			}
		}

		return CreatePrimitiveAsset(vertices, indices);
	}

	AssetHandle MeshFactory::CreateCapsule(float radius, float height)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		constexpr size_t subdivisionsHeight = 8;
		constexpr size_t ringsBody = subdivisionsHeight + 1;
		constexpr size_t ringsTotal = subdivisionsHeight + ringsBody;
		constexpr size_t numSegments = 12;
		constexpr float radiusModifier = 0.021f;

		std::vector<Vertex> vertices;
		std::vector<Index> indices;
		vertices.reserve(numSegments * ringsTotal);
		indices.reserve((numSegments - 1) * (ringsTotal - 1) * 2);

		const float bodyStep = 1.0f / (float)(ringsBody - 1);
		const float ringStep = 1.0f / (float)(subdivisionsHeight - 1);

		for (size_t ring = 0; ring < subdivisionsHeight / 2; ring++)
		{
			const float ringRadius = std::sin(std::numbers::pi_v<float> * (float)ring * ringStep);
			const float normalY = std::sin(std::numbers::pi_v<float> * ((float)ring * ringStep - 0.5f));
			CalculateCapsuleRing(numSegments, ringRadius, normalY, -0.5f, height, radius + radiusModifier, (float)ring / (float)(ringsTotal - 1), vertices);
		}

		for (size_t ring = 0; ring < ringsBody; ring++)
		{
			CalculateCapsuleRing(numSegments, 1.0f, 0.0f, (float)ring * bodyStep - 0.5f, height, radius + radiusModifier, (float)(ring + subdivisionsHeight / 2) / (float)(ringsTotal - 1), vertices);
		}

		for (size_t ring = subdivisionsHeight / 2; ring < subdivisionsHeight; ring++)
		{
			const float ringRadius = std::sin(std::numbers::pi_v<float> * (float)ring * ringStep);
			const float normalY = std::sin(std::numbers::pi_v<float> * ((float)ring * ringStep - 0.5f));
			CalculateCapsuleRing(numSegments, ringRadius, normalY, 0.5f, height, radius + radiusModifier, (float)(ring + ringsBody) / (float)(ringsTotal - 1), vertices);
		}

		for (size_t ring = 0; ring < ringsTotal - 1; ring++)
		{
			for (size_t segment = 0; segment < numSegments - 1; segment++)
			{
				indices.push_back({
					(uint32_t)(ring * numSegments + segment + 1),
					(uint32_t)(ring * numSegments + segment + 0),
					(uint32_t)((ring + 1) * numSegments + segment + 1)
				});

				indices.push_back({
					(uint32_t)((ring + 1) * numSegments + segment + 0),
					(uint32_t)((ring + 1) * numSegments + segment + 1),
					(uint32_t)(ring * numSegments + segment)
				});
			}
		}

		return CreatePrimitiveAsset(vertices, indices);
	}
}
