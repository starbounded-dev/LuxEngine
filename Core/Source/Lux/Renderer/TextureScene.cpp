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
		if (!m_TextureHandles.empty())
			return;

		m_TextureHandles.push_back(AssetHandle(0));
		m_LastTouchedFrames.push_back(m_FrameIndex);
	}

	void TextureScene::BeginSync(uint32_t frameIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_FrameIndex = frameIndex;
		m_DirtyTextureCount = 0;
		m_DirtyTextureIndices.clear();
		m_DirtyRanges.clear();
		EnsureFallbackTexture();
		m_LastTouchedFrames[FallbackGPUTextureIndex] = m_FrameIndex;
	}

	GPUTextureIndex TextureScene::UpsertTexture(AssetHandle textureHandle, bool forceDirty)
	{
		if (!IsTextureHandleValid(textureHandle))
			return InvalidGPUTextureIndex;

		auto idIt = m_TextureIndexByHandle.find(textureHandle);
		GPUTextureIndex textureIndex = InvalidGPUTextureIndex;
		bool created = false;

		if (idIt == m_TextureIndexByHandle.end())
		{
			created = true;
			if (!m_FreeTextureIndices.empty())
			{
				textureIndex = m_FreeTextureIndices.back();
				m_FreeTextureIndices.pop_back();
			}
			else
			{
				textureIndex = (GPUTextureIndex)m_TextureHandles.size();
				m_TextureHandles.emplace_back();
				m_LastTouchedFrames.emplace_back(0);
			}

			m_TextureIndexByHandle[textureHandle] = textureIndex;
			m_TextureHandles[textureIndex] = textureHandle;
		}
		else
		{
			textureIndex = idIt->second;
		}

		m_LastTouchedFrames[textureIndex] = m_FrameIndex;

		if (created || forceDirty)
			MarkTextureDirty(textureIndex);

		return textureIndex;
	}

	void TextureScene::EndSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		for (GPUTextureIndex textureIndex = 1; textureIndex < m_TextureHandles.size(); textureIndex++)
		{
			if (m_LastTouchedFrames[textureIndex] == m_FrameIndex)
				continue;

			const AssetHandle textureHandle = m_TextureHandles[textureIndex];
			if (!textureHandle)
				continue;

			m_TextureIndexByHandle.erase(textureHandle);
			m_TextureHandles[textureIndex] = AssetHandle(0);
			m_LastTouchedFrames[textureIndex] = 0;
			m_FreeTextureIndices.push_back(textureIndex);
			MarkTextureDirty(textureIndex);
		}

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
		m_FrameIndex = 0;
		m_DirtyTextureCount = 0;
		m_TextureHandles.clear();
		m_LastTouchedFrames.clear();
		m_FreeTextureIndices.clear();
		m_TextureIndexByHandle.clear();
		m_DirtyTextureIndices.clear();
		m_DirtyRanges.clear();
		EnsureFallbackTexture();
	}

	void TextureScene::MarkTextureDirty(GPUTextureIndex textureIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (textureIndex >= m_TextureHandles.size())
			return;

		m_DirtyTextureIndices.push_back(textureIndex);
	}

}
