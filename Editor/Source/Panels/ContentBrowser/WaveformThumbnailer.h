// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Ref.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <optional>
#include <set>
#include <vector>

namespace Lux
{
	class ThumbnailCache;

	// Draws waveform thumbnails for audio files and Sounds (a Sound shows its first clip) for the
	// content browser. One file at a time is decoded as a JobSystem job (inline when there are no
	// workers); the main thread rasterizes the peaks and hands the pixels to the ThumbnailCache.
	// Waveforms need the native audio backend; the FMOD build marks every request failed. Main
	// thread only, apart from the job itself, which touches only its own Job.
	class WaveformThumbnailer : public RefCounted
	{
	public:
		~WaveformThumbnailer() override;

		// Queue an asset whose thumbnail is missing or stale. Duplicates, and assets that already
		// failed this session, are ignored.
		void Request(AssetHandle handle);

		// Start the next decode, or finish the current one. Call once per frame.
		void OnUpdate(ThumbnailCache& cache);

		// Drop queued work and forget failures. A decode in flight finishes on its own.
		void Clear();

	private:
		// Shared with the job: it writes Peaks, then sets Done.
		struct Job : public RefCounted
		{
			std::filesystem::path File;
			uint32_t Buckets = 0;
			std::optional<std::vector<float>> Peaks;
			std::atomic<bool> Done = false;
		};

		void Start(AssetHandle handle, uint32_t size);
		void Finish(ThumbnailCache& cache);

	private:
		std::deque<AssetHandle> m_Queue;
		std::set<AssetHandle> m_Queued;
		std::set<AssetHandle> m_Failed;

		AssetHandle m_Current = 0;
		uint32_t m_Size = 0;
		bool m_IsSound = false;
		Ref<Job> m_Job;
	};
}
