// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"

#include "Lux/Renderer/TextureScene.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"

#include <algorithm>

namespace Lux {

	namespace
	{
		bool IsTextureHandleValid(AssetHandle textureHandle)
		{
			if (!textureHandle || !Project::GetAssetManager())
				return false;

			return AssetManager::IsAssetHandleValid(textureHandle)
				&& AssetManager::GetAssetType(textureHandle) == AssetType::Texture;
		}
	}

	TextureScene::TextureScene()
	{
		EnsureFallbackTexture();
	}

	void TextureScene::EnsureFallbackTexture()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_TextureSlots.empty())
			return;

		m_TextureSlots.emplace_back();
		m_RefCounts.push_back(0);
	}

	void TextureScene::BeginSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DirtyTextureCount = 0;
		m_DirtyTextureIndices.clear();
		m_DirtyRanges.clear();
		EnsureFallbackTexture();
	}

	GPUTextureIndex TextureScene::AcquireTexture(AssetHandle textureHandle, TextureColorSpace colorSpace)
	{
		if (!IsTextureHandleValid(textureHandle))
			return InvalidGPUTextureIndex;

		const GPUTextureSlot slot{ textureHandle, colorSpace };
		auto idIt = m_TextureIndexBySlot.find(slot);
		GPUTextureIndex textureIndex = InvalidGPUTextureIndex;
		bool created = false;

		if (idIt == m_TextureIndexBySlot.end())
		{
			created = true;
			if (!m_FreeTextureIndices.empty())
			{
				textureIndex = m_FreeTextureIndices.back();
				m_FreeTextureIndices.pop_back();
			}
			else
			{
				textureIndex = (GPUTextureIndex)m_TextureSlots.size();
				m_TextureSlots.emplace_back();
				m_RefCounts.emplace_back(0);
			}

			m_TextureIndexBySlot[slot] = textureIndex;
			m_TextureSlots[textureIndex] = slot;
		}
		else
		{
			textureIndex = idIt->second;
		}

		m_RefCounts[textureIndex]++;

		if (created)
			MarkTextureDirty(textureIndex);

		return textureIndex;
	}

	void TextureScene::ReleaseTexture(GPUTextureIndex textureIndex)
	{
		if (textureIndex == FallbackGPUTextureIndex || textureIndex >= m_TextureSlots.size())
			return;

		LUX_CORE_ASSERT(m_RefCounts[textureIndex] > 0, "TextureScene: texture slot released more often than acquired");
		if (m_RefCounts[textureIndex] > 0 && --m_RefCounts[textureIndex] == 0)
			m_ReleasedTextureIndices.push_back(textureIndex);
	}

	void TextureScene::EndSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// A slot released and acquired again within the frame keeps its index.
		for (GPUTextureIndex textureIndex : m_ReleasedTextureIndices)
		{
			const GPUTextureSlot slot = m_TextureSlots[textureIndex];
			if (m_RefCounts[textureIndex] != 0 || !slot.Handle)
				continue;

			m_TextureIndexBySlot.erase(slot);
			m_TextureSlots[textureIndex] = {};
			m_FreeTextureIndices.push_back(textureIndex);
			MarkTextureDirty(textureIndex);
		}
		m_ReleasedTextureIndices.clear();

		if (m_DirtyTextureIndices.empty())
			return;

		std::sort(m_DirtyTextureIndices.begin(), m_DirtyTextureIndices.end());
		m_DirtyTextureIndices.erase(std::unique(m_DirtyTextureIndices.begin(), m_DirtyTextureIndices.end()), m_DirtyTextureIndices.end());
		m_DirtyTextureCount = (uint32_t)m_DirtyTextureIndices.size();

		TextureSceneDirtyRange range;
		range.FirstTexture = m_DirtyTextureIndices.front();
		range.TextureCount = 1;
		uint32_t previousTextureIndex = range.FirstTexture;

		for (size_t index = 1; index < m_DirtyTextureIndices.size(); index++)
		{
			const uint32_t textureIndex = m_DirtyTextureIndices[index];
			if (textureIndex == previousTextureIndex + 1)
			{
				range.TextureCount++;
			}
			else
			{
				m_DirtyRanges.push_back(range);
				range.FirstTexture = textureIndex;
				range.TextureCount = 1;
			}

			previousTextureIndex = textureIndex;
		}

		m_DirtyRanges.push_back(range);
	}

	void TextureScene::Clear()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DirtyTextureCount = 0;
		m_TextureSlots.clear();
		m_RefCounts.clear();
		m_FreeTextureIndices.clear();
		m_ReleasedTextureIndices.clear();
		m_TextureIndexBySlot.clear();
		m_DirtyTextureIndices.clear();
		m_DirtyRanges.clear();
		m_Version++;
		EnsureFallbackTexture();
	}

	void TextureScene::MarkTextureDirty(GPUTextureIndex textureIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (textureIndex >= m_TextureSlots.size())
			return;

		m_DirtyTextureIndices.push_back(textureIndex);
		m_Version++;
	}

}
