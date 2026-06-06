#include "lpch.h"

#include "Lux/Renderer/GPUScene.h"

#include <algorithm>
#include <cstring>

namespace Lux {

	namespace
	{
		bool GPUSceneInstanceEquals(const GPUSceneInstanceData& lhs, const GPUSceneInstanceData& rhs)
		{
			return std::memcmp(&lhs, &rhs, sizeof(GPUSceneInstanceData)) == 0;
		}

		GPUSceneInstanceData BuildStoredGPUSceneInstanceData(const GPUSceneInstanceData& data, GPUSceneInstanceID instanceID)
		{
			GPUSceneInstanceData storedData = data;
			storedData.ObjectData.z = instanceID;
			return storedData;
		}
	}

	void GPUScene::BeginSync(uint32_t frameIndex)
	{
		m_FrameIndex = frameIndex;
		m_DirtyInstanceCount = 0;
		m_DirtyInstanceIDs.clear();
		m_DirtyRanges.clear();
	}

	GPUSceneInstanceID GPUScene::UpsertInstance(const GPUSceneInstanceData& data, bool forceDirty)
	{
		const RenderPrimitiveID primitiveID = GetGPUScenePrimitiveID(data);
		if (primitiveID == InvalidRenderPrimitiveID)
			return InvalidGPUSceneInstanceID;

		const InstanceKey key{ primitiveID, GetGPUSceneSubmeshIndex(data) };
		if (auto it = m_InstanceIDByKey.find(key); it != m_InstanceIDByKey.end())
		{
			const GPUSceneInstanceID instanceID = it->second;
			const GPUSceneInstanceData storedData = BuildStoredGPUSceneInstanceData(data, instanceID);
			if (forceDirty || !GPUSceneInstanceEquals(m_Instances[instanceID], storedData))
			{
				m_Instances[instanceID] = storedData;
				MarkInstanceDirty(instanceID);
			}

			return instanceID;
		}

		GPUSceneInstanceID instanceID = InvalidGPUSceneInstanceID;
		if (!m_FreeInstanceIDs.empty())
		{
			instanceID = m_FreeInstanceIDs.back();
			m_FreeInstanceIDs.pop_back();
			m_Instances[instanceID] = BuildStoredGPUSceneInstanceData(data, instanceID);
			m_InstanceKeys[instanceID] = key;
		}
		else
		{
			instanceID = (GPUSceneInstanceID)m_Instances.size();
			m_Instances.push_back(BuildStoredGPUSceneInstanceData(data, instanceID));
			m_InstanceKeys.push_back(key);
		}

		m_InstanceIDByKey[key] = instanceID;
		MarkInstanceDirty(instanceID);
		return instanceID;
	}

	void GPUScene::RemovePrimitive(RenderPrimitiveID primitiveID)
	{
		if (primitiveID == InvalidRenderPrimitiveID)
			return;

		std::vector<InstanceKey> removedKeys;
		for (const auto& [key, instanceID] : m_InstanceIDByKey)
		{
			if (key.PrimitiveID == primitiveID)
				removedKeys.push_back(key);
		}

		for (const InstanceKey& key : removedKeys)
		{
			auto it = m_InstanceIDByKey.find(key);
			if (it == m_InstanceIDByKey.end())
				continue;

			const GPUSceneInstanceID instanceID = it->second;
			GPUSceneInstanceData clearedData;
			clearedData.Metadata = glm::uvec4(primitiveID, key.SubmeshIndex, 0, (uint32_t)GPUSceneInstanceFlags::None);
			clearedData.ObjectData.z = instanceID;
			m_Instances[instanceID] = clearedData;
			m_InstanceKeys[instanceID] = {};
			m_FreeInstanceIDs.push_back(instanceID);
			MarkInstanceDirty(instanceID);
			m_InstanceIDByKey.erase(it);
		}
	}

	void GPUScene::EndSync()
	{
		if (m_DirtyInstanceIDs.empty())
			return;

		std::sort(m_DirtyInstanceIDs.begin(), m_DirtyInstanceIDs.end());
		m_DirtyInstanceIDs.erase(std::unique(m_DirtyInstanceIDs.begin(), m_DirtyInstanceIDs.end()), m_DirtyInstanceIDs.end());
		m_DirtyInstanceCount = (uint32_t)m_DirtyInstanceIDs.size();

		uint32_t rangeStart = m_DirtyInstanceIDs.front();
		uint32_t previous = rangeStart;

		for (size_t index = 1; index < m_DirtyInstanceIDs.size(); index++)
		{
			const uint32_t current = m_DirtyInstanceIDs[index];
			if (current == previous + 1)
			{
				previous = current;
				continue;
			}

			m_DirtyRanges.push_back({ rangeStart, previous - rangeStart + 1 });
			rangeStart = current;
			previous = current;
		}

		m_DirtyRanges.push_back({ rangeStart, previous - rangeStart + 1 });
	}

	void GPUScene::Clear()
	{
		m_FrameIndex = 0;
		m_DirtyInstanceCount = 0;
		m_Instances.clear();
		m_InstanceKeys.clear();
		m_FreeInstanceIDs.clear();
		m_InstanceIDByKey.clear();
		m_DirtyInstanceIDs.clear();
		m_DirtyRanges.clear();
	}

	void GPUScene::MarkInstanceDirty(GPUSceneInstanceID instanceID)
	{
		if (instanceID == InvalidGPUSceneInstanceID)
			return;

		m_DirtyInstanceIDs.push_back(instanceID);
	}

}
