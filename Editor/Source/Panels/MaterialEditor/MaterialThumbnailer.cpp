// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialThumbnailer.h"
#include "MaterialPreview.h"

#include "Panels/ThumbnailCache.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/Renderer.h"

namespace Lux
{
	namespace
	{
		// Frames rendered before the readback, so the dynamic sky, IBL and the GPU material table
		// have settled on the new material.
		constexpr uint32_t kWarmupFrames = 4;

		// Give up on a job that never produces an image (renderer not ready, readback lost).
		constexpr uint32_t kMaxWaitFrames = 240;

		// Keep the preview renderer alive this long after the queue empties: saving a material
		// requests a new thumbnail right away, and recreating a SceneRenderer costs more than
		// holding one for a few seconds.
		constexpr uint32_t kIdleFramesBeforeRelease = 600;
	}

	MaterialThumbnailer::MaterialThumbnailer() = default;
	MaterialThumbnailer::~MaterialThumbnailer() = default;

	void MaterialThumbnailer::Request(AssetHandle materialHandle)
	{
		if (!materialHandle || materialHandle == m_Current || m_Queued.contains(materialHandle) || m_Failed.contains(materialHandle))
			return;

		m_Queue.push_back(materialHandle);
		m_Queued.insert(materialHandle);
	}

	void MaterialThumbnailer::Clear()
	{
		m_Queue.clear();
		m_Queued.clear();
		m_Failed.clear();
		m_Current = 0;
		m_Readback = nullptr;
		m_Preview = nullptr;
	}

	void MaterialThumbnailer::StartNext()
	{
		while (!m_Queue.empty())
		{
			const AssetHandle handle = m_Queue.front();
			m_Queue.pop_front();
			m_Queued.erase(handle);

			if (AssetManager::GetAssetType(handle) != AssetType::Material)
				continue;

			if (!m_Preview)
				m_Preview = Ref<MaterialPreview>::Create();

			m_Current = handle;
			m_FramesRendered = 0;
			m_FramesWaited = 0;
			m_Readback = nullptr;
			m_Preview->SetMaterial(handle);
			return;
		}
	}

	void MaterialThumbnailer::FinishJob(const char* failureReason)
	{
		if (failureReason)
		{
			LUX_CORE_WARN_TAG("Editor", "Material thumbnail for {} failed ({}); not retried until the thumbnail cache is cleared", (uint64_t)m_Current, failureReason);
			m_Failed.insert(m_Current);
		}

		m_Current = 0;
		m_Readback = nullptr;
		m_FramesWaited = 0;
	}

	void MaterialThumbnailer::OnUpdate(ThumbnailCache& cache)
	{
		LUX_PROFILE_FUNCTION("MaterialThumbnailer::OnUpdate");

		if (!m_Current)
			StartNext();

		if (!m_Current)
		{
			// m_FramesWaited doubles as the idle counter between jobs.
			if (m_Preview && ++m_FramesWaited > kIdleFramesBeforeRelease)
			{
				m_Preview = nullptr;
				m_FramesWaited = 0;
			}
			return;
		}

		if (!m_Readback)
		{
			const uint32_t size = cache.GetThumbnailSize();
			Ref<Image2D> image = m_Preview->Render(size, size);
			if (!image)
			{
				if (++m_FramesWaited > kMaxWaitFrames)
					FinishJob("the preview renderer never became ready");
				return;
			}

			if (++m_FramesRendered < kWarmupFrames)
				return;

			// The final composite is RGBA8, which is what the thumbnail cache stores. Anything else
			// (a debug view, a format change) would be misread, so skip it rather than write garbage.
			if (image->GetSpecification().Format != ImageFormat::RGBA)
			{
				FinishJob("preview output is not RGBA8");
				return;
			}

			// Queued after this frame's preview render, so the copy sees the finished image. The
			// readback submits and waits on the render thread, never racing its queue submissions.
			Ref<Readback> readback = Ref<Readback>::Create();
			m_Readback = readback;
			Renderer::Submit([image, readback]() mutable
			{
				image->CopyToHostBuffer(readback->Pixels);
				readback->Width = image->GetWidth();
				readback->Height = image->GetHeight();

				// Rendered rows are top-down; thumbnails are stored bottom-up like imported
				// textures (the content browser draws them with flipped UVs).
				if (readback->Pixels)
				{
					const size_t rowSize = (size_t)readback->Width * 4;
					byte* data = readback->Pixels.As<byte>();
					for (uint32_t top = 0, bottom = readback->Height - 1; top < bottom; ++top, --bottom)
						std::swap_ranges(data + top * rowSize, data + (top + 1) * rowSize, data + bottom * rowSize);
				}
				readback->Ready.store(true, std::memory_order_release);
			});
			m_FramesWaited = 0;
			return;
		}

		if (!m_Readback->Ready.load(std::memory_order_acquire))
		{
			if (++m_FramesWaited > kMaxWaitFrames)
				FinishJob("the GPU readback never completed");
			return;
		}

		if (!m_Readback->Pixels)
		{
			FinishJob("the GPU readback returned no pixels");
			return;
		}

		cache.SetThumbnailPixels(m_Current, m_Readback->Pixels, m_Readback->Width, m_Readback->Height, cache.GetAssetTimestamp(m_Current));
		FinishJob(nullptr);
	}
}
