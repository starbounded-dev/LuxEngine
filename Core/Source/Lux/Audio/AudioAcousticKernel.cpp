// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#include "lpch.h"
#include "AudioAcousticKernel.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/StorageBuffer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include <atomic>

namespace Lux
{
	namespace
	{
		constexpr uint32_t kGroupSize = 64; // SoundwaveReverb.glsl local_size_x

		using ReverbRequest = Soundwave::FlatReverbRequest;
		using GpuRay = Soundwave::FlatReverbRay;

		Ref<StorageBuffer> Upload(Ref<RenderCommandBuffer> commands, const void* data, size_t bytes, const char* name)
		{
			StorageBufferSpecification spec;
			spec.DebugName = name;
			// Vulkan has no empty buffers; a scene without geometry binds one empty record.
			const auto size = static_cast<uint32_t>(std::max<size_t>(bytes, 64));
			auto buffer = StorageBuffer::Create(size, spec);
			if (data && bytes) // request and ray buffers are only allocated here
				buffer->SetData(commands, data, static_cast<uint32_t>(bytes));
			return buffer;
		}
	}

	// Shared with render-thread lambdas, so a kernel destroyed with a dispatch in flight is safe.
	struct AudioAcousticKernel::Readback : RefCounted
	{
		NRIBuffer Buffer;           // render thread
		uint64_t Recorded = 0;      // render thread: frame that recorded the dispatch
		uint64_t Dispatch = 0;      // render thread: which dispatch Buffer holds
		std::vector<GpuRay> Rays;   // written by the render thread before Ready
		std::atomic<uint64_t> Ready{ 0 };
		std::atomic<bool> PollQueued{ false }; // at most one check queued, even while frames do not render
	};

	AudioAcousticKernel::~AudioAcousticKernel() = default;

	Ref<AudioAcousticKernel> AudioAcousticKernel::Create()
	{
		const auto& shaders = Renderer::GetShaderLibrary()->GetShaders();
		const auto found = shaders.find("SoundwaveReverb");
		if (found == shaders.end() || !found->second)
		{
			LUX_CORE_WARN_TAG("Audio", "SoundwaveReverb shader is not loaded; listener reverb is measured on the CPU");
			return nullptr;
		}
		Ref<AudioAcousticKernel> kernel = Ref<AudioAcousticKernel>::Create();
		kernel->m_Shader = found->second;
		kernel->m_Pipeline = PipelineCompute::Create(kernel->m_Shader);
		kernel->m_Commands = RenderCommandBuffer::Create(0, "Soundwave Reverb");
		kernel->m_Readback = Ref<Readback>::Create();
		return kernel;
	}

	bool AudioAcousticKernel::Bind(const Ref<Geometry>& geometry, uint32_t rays)
	{
		const auto& flat = geometry->Flat;
		m_Nodes = Upload(m_Commands, flat.Nodes.data(), flat.Nodes.size() * sizeof(Soundwave::FlatNode), "SoundwaveNodes");
		m_Order = Upload(m_Commands, flat.Order.data(), flat.Order.size() * sizeof(uint32_t), "SoundwaveOrder");
		m_Triangles = Upload(m_Commands, flat.Triangles.data(), flat.Triangles.size() * sizeof(Soundwave::FlatTriangle), "SoundwaveTriangles");
		m_Request = Upload(m_Commands, nullptr, sizeof(ReverbRequest), "SoundwaveReverbRequest");
		m_Rays = Upload(m_Commands, nullptr, rays * sizeof(GpuRay), "SoundwaveReverbRays");
		// Rebuilt only when the geometry generation or ray count changes, never per measurement.
		ComputePassSpecification spec;
		spec.DebugName = "SoundwaveReverb";
		spec.Pipeline = m_Pipeline;
		m_Pass = ComputePass::Create(spec);
		m_Pass->SetInput("SoundwaveNodes", m_Nodes);
		m_Pass->SetInput("SoundwaveOrder", m_Order);
		m_Pass->SetInput("SoundwaveTriangles", m_Triangles);
		m_Pass->SetInput("SoundwaveReverbRequest", m_Request);
		m_Pass->SetInput("SoundwaveReverbRays", m_Rays);
		if (!m_Pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "SoundwaveReverb compute pass failed validation");
			m_Pass = nullptr;
			return false;
		}
		m_Pass->Bake();
		m_Bound = geometry;
		m_BoundRays = rays;
		return true;
	}

	bool AudioAcousticKernel::Dispatch(const Ref<Geometry>& geometry, Soundwave::Position listener,
		const Soundwave::Settings& settings, const Soundwave::MaterialTable& materials)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!geometry || !settings.Validate())
			return false;
		m_Commands->Begin();
		if ((m_Bound != geometry || m_BoundRays != settings.ReverbRays) && !Bind(geometry, settings.ReverbRays))
		{
			m_Commands->End();
			return false;
		}
		ReverbRequest block{};
		block.Listener[0] = listener.X; block.Listener[1] = listener.Y; block.Listener[2] = listener.Z;
		block.Config[0] = settings.ReverbRays;
		block.Config[1] = settings.ReverbBounces;
		block.Config[2] = geometry->Flat.Roots[0];
		block.Config[3] = geometry->Flat.Roots[1];
		block.Params[0] = settings.Absorption;
		for (size_t i = 0; i < materials.size(); ++i)
		{
			block.Materials[i][0] = materials[i].AbsorptionLF;
			block.Materials[i][1] = materials[i].AbsorptionHF;
			block.Materials[i][2] = materials[i].Scattering;
		}
		m_Request->SetData(m_Commands, &block, sizeof(block));
		Renderer::BeginComputePass(m_Commands, m_Pass);
		Renderer::DispatchCompute(m_Commands, m_Pass, nullptr, { (settings.ReverbRays + kGroupSize - 1) / kGroupSize, 1, 1 });
		Renderer::EndComputePass(m_Commands, m_Pass);
		const uint64_t id = ++m_Dispatch;
		const uint64_t bytes = static_cast<uint64_t>(settings.ReverbRays) * sizeof(GpuRay);
		Renderer::Submit([commands = m_Commands, rays = m_Rays, readback = m_Readback, id, bytes]() mutable
		{
			if (!readback->Buffer || readback->Buffer.GetSize() != bytes)
			{
				RHIBufferDesc desc;
				desc.Size = bytes;
				desc.Access = CPUAccess::Read;
				desc.DebugName = "SoundwaveReverbReadback";
				readback->Buffer = NRIBuffer::Create(desc);
				if (!readback->Buffer)
					return;
			}
			commands->RT_RequireBufferState(rays->GetBuffer().GetTracked(), ResourceState::CopySource);
			nri::CommandBuffer* nriCommands = commands->RT_BeginNRITransfer();
			if (!nriCommands)
				return;
			RHIDevice::API().CmdCopyBuffer(*nriCommands, *readback->Buffer.Get(), 0, *rays->GetBuffer().Get(), 0, bytes);
			commands->RT_EndNRISegment();
			readback->Recorded = Renderer::RT_GetFrameNumber();
			readback->Dispatch = id;
		});
		m_Commands->End();
		m_Commands->Submit();
		return true;
	}

	bool AudioAcousticKernel::Poll(std::vector<Soundwave::ReverbRay>& rays)
	{
		if (m_Readback->Ready.load(std::memory_order_acquire) == m_Dispatch)
		{
			rays.resize(m_Readback->Rays.size());
			for (size_t i = 0; i < rays.size(); ++i)
			{
				const auto& ray = m_Readback->Rays[i];
				rays[i] = { ray.Absorbed, ray.Travelled, ray.Hits, ray.Traces & 0x7FFFFFFFu, ray.Traces >> 31 };
			}
			return true;
		}
		if (m_Readback->PollQueued.exchange(true))
			return false;
		// RT_BeginFrame of frame Recorded + FramesInFlight has waited for the recording frame, so
		// the copy is complete then; earlier the lambda simply leaves it for a later update.
		Renderer::Submit([readback = m_Readback, expected = m_Dispatch]() mutable
		{
			readback->PollQueued.store(false);
			if (readback->Dispatch != expected || readback->Ready.load(std::memory_order_relaxed) == expected ||
				Renderer::RT_GetFrameNumber() < readback->Recorded + Renderer::GetConfig().FramesInFlight)
				return;
			const auto count = readback->Buffer.GetSize() / sizeof(GpuRay);
			const auto* mapped = static_cast<const GpuRay*>(readback->Buffer.Map(0, readback->Buffer.GetSize()));
			if (!mapped)
				return;
			readback->Rays.assign(mapped, mapped + count);
			readback->Buffer.Unmap();
			readback->Ready.store(expected, std::memory_order_release);
		});
		return false;
	}
}
