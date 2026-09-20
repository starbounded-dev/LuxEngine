// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Buffer.h"
#include "Lux/Core/Ref.h"

#include <atomic>
#include <deque>
#include <set>

namespace Lux
{
	class MaterialPreview;
	class ThumbnailCache;

	// Renders material thumbnails for the content browser, one material at a time, with its own
	// MaterialPreview (so the Material Editor's view is never disturbed). The pixels are read back
	// on the render thread and handed to the ThumbnailCache as CPU data. Main thread only.
	class MaterialThumbnailer : public RefCounted
	{
	public:
		MaterialThumbnailer();
		~MaterialThumbnailer() override; // out of line: MaterialPreview is incomplete here

		// Queue a material whose thumbnail is missing or stale. Duplicates, and materials that
		// already failed this session, are ignored.
		void Request(AssetHandle materialHandle);

		// Advance the current job by one step. Call once per frame.
		void OnUpdate(ThumbnailCache& cache);

		// Drop all work, forget failures and release the preview renderer.
		void Clear();

	private:
		// Written on the render thread, read on the main thread once Ready is set.
		struct Readback : public RefCounted
		{
			Buffer Pixels;
			uint32_t Width = 0;
			uint32_t Height = 0;
			std::atomic<bool> Ready = false;

			~Readback() override { Pixels.Release(); }
		};

		void StartNext();
		void FinishJob(const char* failureReason); // null on success

	private:
		Ref<MaterialPreview> m_Preview;

		std::deque<AssetHandle> m_Queue;
		std::set<AssetHandle> m_Queued;
		std::set<AssetHandle> m_Failed; // not retried every frame; Clear() resets

		AssetHandle m_Current = 0;
		uint32_t m_FramesRendered = 0;
		uint32_t m_FramesWaited = 0;
		Ref<Readback> m_Readback;
	};
}
