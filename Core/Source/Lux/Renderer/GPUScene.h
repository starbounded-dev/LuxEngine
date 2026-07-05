#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/UUID.h"
#include "Lux/Renderer/MaterialScene.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Lux {

	using RenderPrimitiveID = uint32_t;
	inline constexpr RenderPrimitiveID InvalidRenderPrimitiveID = 0;

	using GPUSceneInstanceID = uint32_t;
	inline constexpr GPUSceneInstanceID InvalidGPUSceneInstanceID = std::numeric_limits<GPUSceneInstanceID>::max();

	enum class RenderProxyDirtyFlags : uint32_t
	{
		None = 0,
		Created = BIT(0),
		Removed = BIT(1),
		Transform = BIT(2),
		Mesh = BIT(3),
		Material = BIT(4),
		Visibility = BIT(5),
		Selection = BIT(6)
	};

	inline constexpr RenderProxyDirtyFlags operator|(RenderProxyDirtyFlags lhs, RenderProxyDirtyFlags rhs)
	{
		return (RenderProxyDirtyFlags)((uint32_t)lhs | (uint32_t)rhs);
	}

	inline constexpr RenderProxyDirtyFlags operator&(RenderProxyDirtyFlags lhs, RenderProxyDirtyFlags rhs)
	{
		return (RenderProxyDirtyFlags)((uint32_t)lhs & (uint32_t)rhs);
	}

	inline RenderProxyDirtyFlags& operator|=(RenderProxyDirtyFlags& lhs, RenderProxyDirtyFlags rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	inline constexpr bool HasDirtyFlag(RenderProxyDirtyFlags flags, RenderProxyDirtyFlags mask)
	{
		return (uint32_t)(flags & mask) != 0;
	}

	enum class GPUSceneInstanceFlags : uint32_t
	{
		None = 0,
		Visible = BIT(0),
		Selected = BIT(1)
	};

	inline constexpr GPUSceneInstanceFlags operator|(GPUSceneInstanceFlags lhs, GPUSceneInstanceFlags rhs)
	{
		return (GPUSceneInstanceFlags)((uint32_t)lhs | (uint32_t)rhs);
	}

	inline GPUSceneInstanceFlags& operator|=(GPUSceneInstanceFlags& lhs, GPUSceneInstanceFlags rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	struct GPUSceneInstanceData
	{
		glm::vec4 TransformRows[3] = {};
		glm::vec4 PreviousTransformRows[3] = {};
		glm::vec4 BoundsSphere = glm::vec4(0.0f);
		glm::uvec4 Metadata = glm::uvec4(0); // x = primitive ID, y = submesh index, z = RenderMaterialID, w = flags
		glm::uvec4 ObjectData = glm::uvec4(0); // x/y = entity UUID low/high, z = GPUScene instance ID, w = reserved
	};

	inline RenderPrimitiveID GetGPUScenePrimitiveID(const GPUSceneInstanceData& data)
	{
		return data.Metadata.x;
	}

	inline uint32_t GetGPUSceneSubmeshIndex(const GPUSceneInstanceData& data)
	{
		return data.Metadata.y;
	}

	inline uint32_t GetGPUSceneMaterialIndex(const GPUSceneInstanceData& data)
	{
		return data.Metadata.z;
	}

	inline RenderMaterialID GetGPUSceneRenderMaterialID(const GPUSceneInstanceData& data)
	{
		return data.Metadata.z;
	}

	inline uint32_t GetGPUSceneInstanceFlags(const GPUSceneInstanceData& data)
	{
		return data.Metadata.w;
	}

	inline GPUSceneInstanceID GetGPUSceneStoredInstanceID(const GPUSceneInstanceData& data)
	{
		return data.ObjectData.z;
	}

	struct GPUSceneInstanceRef
	{
		GPUSceneInstanceID InstanceID = InvalidGPUSceneInstanceID;
		GPUSceneInstanceData Data;
	};

	struct GPUSceneDirtyRange
	{
		uint32_t FirstInstance = 0;
		uint32_t InstanceCount = 0;
	};

	class GPUScene
	{
	public:
		void BeginSync(uint32_t frameIndex);
		GPUSceneInstanceID UpsertInstance(const GPUSceneInstanceData& data, bool forceDirty = false);
		void RemovePrimitive(RenderPrimitiveID primitiveID);
		void EndSync();
		void Clear();

		const std::vector<GPUSceneInstanceData>& GetInstances() const { return m_Instances; }
		const GPUSceneInstanceData& GetInstance(GPUSceneInstanceID instanceID) const { return m_Instances.at(instanceID); }
		const std::vector<GPUSceneDirtyRange>& GetDirtyRanges() const { return m_DirtyRanges; }
		bool HasDirtyInstances() const { return m_DirtyInstanceCount > 0; }
		uint32_t GetDirtyInstanceCount() const { return m_DirtyInstanceCount; }
		size_t GetActiveInstanceCount() const { return m_InstanceIDByKey.size(); }

	private:
		struct InstanceKey
		{
			RenderPrimitiveID PrimitiveID = InvalidRenderPrimitiveID;
			uint32_t SubmeshIndex = 0;

			bool operator==(const InstanceKey& other) const
			{
				return PrimitiveID == other.PrimitiveID && SubmeshIndex == other.SubmeshIndex;
			}
		};

		struct InstanceKeyHasher
		{
			size_t operator()(const InstanceKey& key) const
			{
				size_t seed = std::hash<uint32_t>{}(key.PrimitiveID);
				seed ^= std::hash<uint32_t>{}(key.SubmeshIndex) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				return seed;
			}
		};

		void MarkInstanceDirty(GPUSceneInstanceID instanceID);

	private:
		uint32_t m_FrameIndex = 0;
		uint32_t m_DirtyInstanceCount = 0;

		std::vector<GPUSceneInstanceData> m_Instances;
		std::vector<InstanceKey> m_InstanceKeys;
		std::vector<GPUSceneInstanceID> m_FreeInstanceIDs;
		std::unordered_map<InstanceKey, GPUSceneInstanceID, InstanceKeyHasher> m_InstanceIDByKey;
		std::unordered_map<RenderPrimitiveID, std::vector<GPUSceneInstanceID>> m_InstanceIDsByPrimitive;

		std::vector<GPUSceneInstanceID> m_DirtyInstanceIDs;
		std::vector<GPUSceneDirtyRange> m_DirtyRanges;
	};

}
