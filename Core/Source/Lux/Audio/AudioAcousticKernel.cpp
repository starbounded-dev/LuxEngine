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
#include <bit>
#include <limits>
#include <new>

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
		std::atomic<uint64_t> Failed{ 0 };
		std::atomic<bool> PollQueued{ false }; // at most one check queued, even while frames do not render
	};
	struct AudioAcousticKernel::VisibilityReadback : RefCounted
	{
		NRIBuffer Buffer;
		uint64_t Recorded = 0, Dispatch = 0;
		std::vector<uint32_t> Results;
		std::atomic<uint64_t> Ready{ 0 }, Failed{ 0 };
		std::atomic<bool> PollQueued{ false };
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
		const auto visibility = shaders.find("SoundwaveVisibility");
		if (visibility != shaders.end() && visibility->second)
		{
			kernel->m_VisibilityShader = visibility->second;
			kernel->m_VisibilityPipeline = PipelineCompute::Create(kernel->m_VisibilityShader);
			kernel->m_VisibilityCommands = RenderCommandBuffer::Create(0, "Soundwave Visibility");
			kernel->m_VisibilityReadback = Ref<VisibilityReadback>::Create();
		}
		else
			LUX_CORE_WARN_TAG("Audio", "SoundwaveVisibility shader is not loaded; sources use CPU propagation");
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

	bool AudioAcousticKernel::BindVisibility(const Ref<Geometry>& geometry, uint32_t segments, uint32_t portals)
	{
		const auto& flat = geometry->Flat;
		const uint32_t segmentCapacity = std::bit_ceil(segments);
		const uint32_t portalCapacity = std::bit_ceil(std::max(1u, portals));
		if (m_VisibilityBound != geometry)
		{
			if (m_Bound == geometry && m_Nodes && m_Order && m_Triangles)
			{
				m_VisibilityNodes = m_Nodes;
				m_VisibilityOrder = m_Order;
				m_VisibilityTriangles = m_Triangles;
			}
			else
			{
				m_VisibilityNodes = Upload(m_VisibilityCommands, flat.Nodes.data(), flat.Nodes.size() * sizeof(Soundwave::FlatNode), "SoundwaveNodes");
				m_VisibilityOrder = Upload(m_VisibilityCommands, flat.Order.data(), flat.Order.size() * sizeof(uint32_t), "SoundwaveOrder");
				m_VisibilityTriangles = Upload(m_VisibilityCommands, flat.Triangles.data(), flat.Triangles.size() * sizeof(Soundwave::FlatTriangle), "SoundwaveTriangles");
			}
		}
		if (!m_VisibilityRequest)
			m_VisibilityRequest = Upload(m_VisibilityCommands, nullptr, sizeof(Soundwave::FlatVisibilityRequest), "SoundwaveVisibilityRequest");
		if (segmentCapacity > m_VisibilityCapacity)
		{
			m_VisibilitySegments = Upload(m_VisibilityCommands, nullptr, static_cast<size_t>(segmentCapacity) * sizeof(Soundwave::FlatVisibilitySegment), "SoundwaveVisibilitySegments");
			m_VisibilityResults = Upload(m_VisibilityCommands, nullptr, static_cast<size_t>(segmentCapacity) * sizeof(uint32_t), "SoundwaveVisibilityResults");
			m_VisibilityCapacity = segmentCapacity;
		}
		if (portalCapacity > m_VisibilityPortalCapacity)
		{
			m_VisibilityPortals = Upload(m_VisibilityCommands, nullptr, static_cast<size_t>(portalCapacity) * sizeof(Soundwave::FlatPortal), "SoundwaveVisibilityPortals");
			m_VisibilityPortalCapacity = portalCapacity;
		}
		if (!m_VisibilityNodes || !m_VisibilityOrder || !m_VisibilityTriangles || !m_VisibilityRequest ||
			!m_VisibilitySegments || !m_VisibilityResults || !m_VisibilityPortals)
		{
			LUX_CORE_ERROR_TAG("Audio", "Soundwave visibility buffers could not be allocated");
			return false;
		}
		ComputePassSpecification spec;
		spec.DebugName = "SoundwaveVisibility";
		spec.Pipeline = m_VisibilityPipeline;
		auto pass = ComputePass::Create(spec);
		pass->SetInput("SoundwaveNodes", m_VisibilityNodes);
		pass->SetInput("SoundwaveOrder", m_VisibilityOrder);
		pass->SetInput("SoundwaveTriangles", m_VisibilityTriangles);
		pass->SetInput("SoundwaveVisibilityRequest", m_VisibilityRequest);
		pass->SetInput("SoundwaveVisibilitySegments", m_VisibilitySegments);
		pass->SetInput("SoundwaveVisibilityPortals", m_VisibilityPortals);
		pass->SetInput("SoundwaveVisibilityResults", m_VisibilityResults);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "Soundwave visibility compute pass failed validation");
			return false;
		}
		pass->Bake();
		m_VisibilityPass = std::move(pass);
		m_VisibilityBound = geometry;
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
				{
					readback->Failed.store(id, std::memory_order_release);
					return;
				}
			}
			commands->RT_RequireBufferState(rays->GetBuffer().GetTracked(), ResourceState::CopySource);
			nri::CommandBuffer* nriCommands = commands->RT_BeginNRITransfer();
			if (!nriCommands)
			{
				readback->Failed.store(id, std::memory_order_release);
				return;
			}
			RHIDevice::API().CmdCopyBuffer(*nriCommands, *readback->Buffer.Get(), 0, *rays->GetBuffer().Get(), 0, bytes);
			commands->RT_EndNRISegment();
			readback->Recorded = Renderer::RT_GetFrameNumber();
			readback->Dispatch = id;
		});
		m_Commands->End();
		m_Commands->Submit();
		return true;
	}

	AudioAcoustics::ReverbBackend::PollResult AudioAcousticKernel::Poll(std::vector<Soundwave::ReverbRay>& rays)
	{
		if (m_Readback->Failed.load(std::memory_order_acquire) == m_Dispatch)
			return PollResult::Failed;
		if (m_Readback->Ready.load(std::memory_order_acquire) == m_Dispatch)
		{
			try
			{
				rays.resize(m_Readback->Rays.size());
			}
			catch (const std::bad_alloc&)
			{
				m_Readback->Failed.store(m_Dispatch, std::memory_order_release);
				return PollResult::Failed;
			}
			for (size_t i = 0; i < rays.size(); ++i)
			{
				const auto& ray = m_Readback->Rays[i];
				rays[i] = { ray.Absorbed, ray.Travelled, ray.Hits, ray.Traces & 0x7FFFFFFFu, ray.Traces >> 31 };
			}
			return PollResult::Ready;
		}
		if (m_Readback->PollQueued.exchange(true))
			return PollResult::Pending;
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
			{
				readback->Failed.store(expected, std::memory_order_release);
				return;
			}
			try
			{
				readback->Rays.assign(mapped, mapped + count);
			}
			catch (const std::bad_alloc&)
			{
				readback->Buffer.Unmap();
				readback->Failed.store(expected, std::memory_order_release);
				return;
			}
			readback->Buffer.Unmap();
			readback->Ready.store(expected, std::memory_order_release);
		});
		return PollResult::Pending;
	}

	bool AudioAcousticKernel::DispatchVisibility(const Ref<Geometry>& geometry,
		std::span<const Soundwave::FlatVisibilitySegment> segments, std::span<const Soundwave::FlatPortal> portals)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		constexpr size_t kMaximumSegments = 262144;
		if (!m_VisibilityShader || !geometry || segments.empty() || segments.size() > kMaximumSegments || portals.size() > 4096)
			return false;
		m_VisibilityCommands->Begin();
		if ((m_VisibilityBound != geometry || segments.size() > m_VisibilityCapacity || portals.size() > m_VisibilityPortalCapacity) &&
			!BindVisibility(geometry, static_cast<uint32_t>(segments.size()), static_cast<uint32_t>(portals.size())))
		{
			m_VisibilityCommands->End();
			return false;
		}
		Soundwave::FlatVisibilityRequest block{};
		block.Roots[0] = geometry->Flat.Roots[0];
		block.Roots[1] = geometry->Flat.Roots[1];
		block.SegmentCount = static_cast<uint32_t>(segments.size());
		block.PortalCount = static_cast<uint32_t>(portals.size());
		m_VisibilityRequest->SetData(m_VisibilityCommands, &block, sizeof(block));
		m_VisibilitySegments->SetData(m_VisibilityCommands, segments.data(), static_cast<uint32_t>(segments.size_bytes()));
		if (!portals.empty())
			m_VisibilityPortals->SetData(m_VisibilityCommands, portals.data(), static_cast<uint32_t>(portals.size_bytes()));
		Renderer::BeginComputePass(m_VisibilityCommands, m_VisibilityPass);
		Renderer::DispatchCompute(m_VisibilityCommands, m_VisibilityPass, nullptr,
			{ (block.SegmentCount + kGroupSize - 1) / kGroupSize, 1, 1 });
		Renderer::EndComputePass(m_VisibilityCommands, m_VisibilityPass);
		const uint64_t id = ++m_VisibilityDispatch;
		const uint64_t bytes = static_cast<uint64_t>(block.SegmentCount) * sizeof(uint32_t);
		Renderer::Submit([commands = m_VisibilityCommands, results = m_VisibilityResults,
			readback = m_VisibilityReadback, id, bytes]() mutable
		{
			if (!readback->Buffer || readback->Buffer.GetSize() != bytes)
			{
				RHIBufferDesc desc;
				desc.Size = bytes;
				desc.Access = CPUAccess::Read;
				desc.DebugName = "SoundwaveVisibilityReadback";
				readback->Buffer = NRIBuffer::Create(desc);
				if (!readback->Buffer)
				{
					readback->Failed.store(id, std::memory_order_release);
					return;
				}
			}
			commands->RT_RequireBufferState(results->GetBuffer().GetTracked(), ResourceState::CopySource);
			nri::CommandBuffer* nriCommands = commands->RT_BeginNRITransfer();
			if (!nriCommands)
			{
				readback->Failed.store(id, std::memory_order_release);
				return;
			}
			RHIDevice::API().CmdCopyBuffer(*nriCommands, *readback->Buffer.Get(), 0, *results->GetBuffer().Get(), 0, bytes);
			commands->RT_EndNRISegment();
			readback->Recorded = Renderer::RT_GetFrameNumber();
			readback->Dispatch = id;
		});
		m_VisibilityCommands->End();
		m_VisibilityCommands->Submit();
		m_VisibilityCount = block.SegmentCount;
		return true;
	}

	AudioAcoustics::ReverbBackend::PollResult AudioAcousticKernel::PollVisibility(std::vector<uint32_t>& results)
	{
		if (!m_VisibilityReadback || !m_VisibilityDispatch)
			return PollResult::Failed;
		if (m_VisibilityReadback->Failed.load(std::memory_order_acquire) == m_VisibilityDispatch)
			return PollResult::Failed;
		if (m_VisibilityReadback->Ready.load(std::memory_order_acquire) == m_VisibilityDispatch)
		{
			try
			{
				results = m_VisibilityReadback->Results;
			}
			catch (const std::bad_alloc&)
			{
				m_VisibilityReadback->Failed.store(m_VisibilityDispatch, std::memory_order_release);
				return PollResult::Failed;
			}
			return results.size() == m_VisibilityCount ? PollResult::Ready : PollResult::Failed;
		}
		if (m_VisibilityReadback->PollQueued.exchange(true))
			return PollResult::Pending;
		Renderer::Submit([readback = m_VisibilityReadback, expected = m_VisibilityDispatch]() mutable
		{
			readback->PollQueued.store(false);
			if (readback->Dispatch != expected || readback->Ready.load(std::memory_order_relaxed) == expected ||
				Renderer::RT_GetFrameNumber() < readback->Recorded + Renderer::GetConfig().FramesInFlight)
				return;
			const auto count = readback->Buffer.GetSize() / sizeof(uint32_t);
			const auto* mapped = static_cast<const uint32_t*>(readback->Buffer.Map(0, readback->Buffer.GetSize()));
			if (!mapped)
			{
				readback->Failed.store(expected, std::memory_order_release);
				return;
			}
			try
			{
				readback->Results.assign(mapped, mapped + count);
			}
			catch (const std::bad_alloc&)
			{
				readback->Buffer.Unmap();
				readback->Failed.store(expected, std::memory_order_release);
				return;
			}
			readback->Buffer.Unmap();
			readback->Ready.store(expected, std::memory_order_release);
		});
		return PollResult::Pending;
	}
}
