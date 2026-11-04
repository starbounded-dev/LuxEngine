// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "WaveformThumbnailer.h"

#include "Panels/ThumbnailCache.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Core/Buffer.h"
#include "Lux/Core/JobSystem.h"
#include "Lux/Project/Project.h"

#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		struct Rgba
		{
			uint8_t R, G, B, A;
		};

		// The editor theme: the dark background, a neutral waveform for audio files and the accent
		// for Sounds, so the two read apart at a glance.
		constexpr Rgba kBackground{ 24, 22, 20, 255 };
		constexpr Rgba kAudioWave{ 168, 158, 146, 255 };
		constexpr Rgba kSoundWave{ 200, 255, 77, 255 };
		constexpr float kWaveHeightFraction = 0.4f; // of the image, each side of the centre line

		// The file a thumbnail shows: the audio file itself, or a Sound's first clip.
		std::filesystem::path ResolveFile(AssetHandle handle, bool& outIsSound)
		{
			Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
			if (!editorAssetManager)
				return {};
			const AssetType type = AssetManager::GetAssetType(handle);
			outIsSound = type == AssetType::Sound;
			if (type == AssetType::Audio)
				return editorAssetManager->GetFileSystemPath(handle);
			if (type != AssetType::Sound)
				return {};
			const Ref<Sound> sound = AssetManager::GetAsset<Sound>(handle);
			const std::vector<AssetHandle> clips = sound ? sound->GetClips() : std::vector<AssetHandle>{};
			if (clips.empty() || AssetManager::GetAssetType(clips.front()) != AssetType::Audio)
				return {};
			return editorAssetManager->GetFileSystemPath(clips.front());
		}
	}

	WaveformThumbnailer::~WaveformThumbnailer() = default; // a job in flight keeps its own Ref<Job>

	void WaveformThumbnailer::Request(AssetHandle handle)
	{
		if (!handle || handle == m_Current || m_Failed.contains(handle) || !m_Queued.insert(handle).second)
			return;
		m_Queue.push_back(handle);
	}

	void WaveformThumbnailer::Clear()
	{
		m_Queue.clear();
		m_Queued.clear();
		m_Failed.clear();
		m_Current = 0;
		m_Job = nullptr;
	}

	void WaveformThumbnailer::OnUpdate(ThumbnailCache& cache)
	{
		if (m_Job)
		{
			if (m_Job->Done.load(std::memory_order_acquire))
				Finish(cache);
			return;
		}
		while (!m_Queue.empty() && !m_Job)
		{
			const AssetHandle handle = m_Queue.front();
			m_Queue.pop_front();
			m_Queued.erase(handle);
			Start(handle, cache.GetThumbnailSize());
		}
	}

	void WaveformThumbnailer::Start(AssetHandle handle, uint32_t size)
	{
		bool isSound = false;
		const std::filesystem::path file = ResolveFile(handle, isSound);
		if (file.empty() || size == 0)
		{
			m_Failed.insert(handle);
			return;
		}
		m_Current = handle;
		m_Size = size;
		m_IsSound = isSound;
		m_Job = Ref<Job>::Create();
		m_Job->File = file;
		m_Job->Buckets = size;
		// The job owns a reference, so it may outlive Clear() or this thumbnailer.
		JobSystem::Submit([job = m_Job]() mutable
		{
			job->Peaks = AudioFileUtils::GetWaveform(job->File, job->Buckets);
			job->Done.store(true, std::memory_order_release);
		});
	}

	void WaveformThumbnailer::Finish(ThumbnailCache& cache)
	{
		const AssetHandle handle = m_Current;
		const Ref<Job> job = m_Job;
		m_Current = 0;
		m_Job = nullptr;
		if (!job->Peaks || job->Peaks->size() != m_Size)
		{
			m_Failed.insert(handle);
			return;
		}

		const uint32_t size = m_Size;
		Buffer pixels;
		pixels.Allocate(static_cast<uint64_t>(size) * size * 4);
		auto* rgba = reinterpret_cast<Rgba*>(pixels.Data);
		std::fill(rgba, rgba + static_cast<size_t>(size) * size, kBackground);
		const Rgba wave = m_IsSound ? kSoundWave : kAudioWave;
		const float centre = static_cast<float>(size) * 0.5f;
		for (uint32_t x = 0; x < size; ++x)
		{
			// At least one pixel each side, so silence still draws the centre line.
			const float extent = std::max(1.0f, (*job->Peaks)[x] * kWaveHeightFraction * static_cast<float>(size));
			const auto top = static_cast<uint32_t>(std::clamp(std::floor(centre - extent), 0.0f, static_cast<float>(size - 1)));
			const auto bottom = static_cast<uint32_t>(std::clamp(std::ceil(centre + extent), 0.0f, static_cast<float>(size)));
			for (uint32_t y = top; y < bottom; ++y)
				rgba[static_cast<size_t>(y) * size + x] = wave;
		}
		// The asset's own timestamp: a Sound's thumbnail refreshes when the Sound is saved.
		cache.SetThumbnailPixels(handle, pixels, size, size, cache.GetAssetTimestamp(handle));
		pixels.Release();
	}
}
