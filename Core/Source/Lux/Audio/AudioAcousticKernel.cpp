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

	struct AudioAcousticKernel::Sources : RefCounted
	{
		std::array<Ref<PipelineCompute>, 4> Pipelines;
		std::array<Ref<ComputePass>, 4> Passes;
		Ref<RenderCommandBuffer> Commands, Completion;
		Ref<Geometry> Bound;
		std::array<Ref<StorageBuffer>, 13> Buffers;
		std::array<uint32_t, 13> Capacity{};
		NRIBuffer Readback;
		uint64_t Serial = 0, Recorded = 0, RecordedSerial = 0;
		std::vector<SourceOutput> Results;
		std::atomic<uint64_t> Ready{0}, Failed{0};
		std::atomic<bool> PollQueued{false};
		struct Layout
		{
			uint32_t Offset, OutputBytes, PathSlots;
		};
		std::vector<Layout> Layouts;
		uint32_t ReadbackBytes = 0;
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
		constexpr std::array names{"SoundwaveOpeningEndpoints", "SoundwaveOpeningAdjacency",
			"SoundwaveOpeningReflections", "SoundwaveOpeningSearch"};
		if (std::all_of(names.begin(), names.end(), [&](const char* name) {
				const auto entry = shaders.find(name);
				return entry != shaders.end() && entry->second;
			}))
		{
			kernel->m_Sources = Ref<Sources>::Create();
			for (size_t i = 0; i < names.size(); ++i)
				kernel->m_Sources->Pipelines[i] = PipelineCompute::Create(shaders.at(names[i]));
			kernel->m_Sources->Commands = RenderCommandBuffer::Create(0, "Soundwave Sources", true, GPUQueue::Compute);
			kernel->m_Sources->Completion = RenderCommandBuffer::Create(0, "Soundwave Source Completion");
		}
		else
			LUX_CORE_WARN_TAG(
				"Audio", "Soundwave source shaders are not loaded; sources use visibility or CPU fallback");
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
	float AudioAcousticKernel::SourceGpuMilliseconds() const
	{
		return m_Sources ? m_Sources->Commands->GetExecutionGPUTime() : 0;
	}

	bool AudioAcousticKernel::SupportsSources() const
	{
		return m_Sources != nullptr;
	}

	bool AudioAcousticKernel::DispatchSources(const Ref<Geometry>& geometry, std::span<const SourceInput> inputs,
		std::span<const Soundwave::FlatPortal> portals)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Sources || !geometry || inputs.empty() || inputs.size() > 64 || portals.size() > 4096)
			return false;
		auto state = m_Sources;
		auto commands = state->Commands;
		constexpr std::array names{"SoundwaveNodes", "SoundwaveOrder", "SoundwaveTriangles", "SoundwaveOpeningRequest",
			"SoundwaveOpeningNodes", "SoundwaveOpeningScratch", "SoundwaveOpeningResult", "SoundwaveOpeningMatrix",
			"SoundwaveOpeningProposals", "SoundwaveOpeningPortals", "SoundwaveReflectionCandidates",
			"SoundwaveReflectionVisibility", "SoundwaveReflectionIndices"};
		const auto& flat = geometry->Flat;
		std::array<size_t, 13> sizes{flat.Nodes.size() * sizeof(Soundwave::FlatNode), flat.Order.size() * 4,
			flat.Triangles.size() * sizeof(Soundwave::FlatTriangle), sizeof(Soundwave::GpuOpeningRequest), 0, 0, 0, 0,
			0, portals.size_bytes(), 0, 0, 0};
		std::vector<Sources::Layout> layouts;
		uint32_t total = 0;
		for (const auto& input : inputs)
		{
			const auto& request = input.Request;
			if (request.Config[0] != input.Nodes.size() || request.Config[0] > 2048 ||
				request.Reflections[0] != input.Reflections.size() || request.Config[2] == 0 ||
				request.Config[2] > 65536 || request.Config[1] == 0 || request.Config[1] > 16 ||
				request.Config[3] > 4096 || request.Initial[3] != portals.size())
				return false;
			const size_t width = input.Nodes.size() + 2;
			sizes[4] = std::max(sizes[4], input.Nodes.size() * sizeof(Soundwave::GpuOpeningNode));
			sizes[5] = std::max(sizes[5], Soundwave::GpuOpeningScratchBytes(request));
			sizes[6] = std::max(sizes[6], Soundwave::GpuOpeningResultBytes(request));
			sizes[7] = std::max(sizes[7], width * width * 4);
			sizes[8] = std::max(sizes[8], input.Nodes.size() * 32);
			sizes[10] = std::max(sizes[10], input.Reflections.size() * sizeof(Soundwave::GpuReflection));
			sizes[11] = std::max(sizes[11], input.Reflections.size() * 8);
			sizes[12] = std::max(sizes[12], size_t(request.Config[3]) * 4);
			const uint32_t bytes = static_cast<uint32_t>(Soundwave::GpuOpeningResultBytes(request));
			layouts.push_back({total, bytes, request.Config[3]});
			total += bytes + request.Config[3] * 4;
		}
		commands->Begin();
		bool rebind = state->Bound != geometry;
		for (size_t i = 0; i < sizes.size(); ++i)
		{
			if (sizes[i] > UINT32_MAX / 2)
			{
				commands->End();
				return false;
			}
			const auto capacity = std::bit_ceil(static_cast<uint32_t>(std::max<size_t>(64, sizes[i])));
			if (!state->Buffers[i] || capacity > state->Capacity[i] || (i < 3 && state->Bound != geometry))
			{
				state->Buffers[i] = Upload(commands, nullptr, capacity, names[i]);
				state->Capacity[i] = capacity;
				rebind = true;
			}
			if (!state->Buffers[i])
			{
				commands->End();
				return false;
			}
		}
		if (state->Bound != geometry)
		{
			const std::array<const void*, 3> data{flat.Nodes.data(), flat.Order.data(), flat.Triangles.data()};
			for (size_t i = 0; i < data.size(); ++i)
				if (sizes[i])
					state->Buffers[i]->SetData(commands, data[i], static_cast<uint32_t>(sizes[i]));
		}
		if (rebind)
		{
			for (size_t stage = 0; stage < state->Passes.size(); ++stage)
			{
				ComputePassSpecification spec;
				spec.Pipeline = state->Pipelines[stage];
				spec.DebugName = "Soundwave Sources " + std::to_string(stage);
				auto pass = ComputePass::Create(spec);
				for (size_t i = 0; i < names.size(); ++i)
					if (pass->GetInputDeclaration(names[i]))
						pass->SetInput(names[i], state->Buffers[i]);
				if (!pass->Validate())
				{
					commands->End();
					return false;
				}
				pass->Bake();
				state->Passes[stage] = std::move(pass);
			}
			state->Bound = geometry;
		}
		const uint64_t id = ++state->Serial;
		state->Layouts = layouts;
		state->ReadbackBytes = total;
		Renderer::Submit([state, id, total]() mutable {
			if (!state->Readback || state->Readback.GetSize() < total)
			{
				RHIBufferDesc desc;
				desc.Size = std::bit_ceil(static_cast<uint64_t>(total));
				desc.Access = CPUAccess::Read;
				desc.DebugName = "SoundwaveSourceReadback";
				state->Readback = NRIBuffer::Create(desc);
				if (!state->Readback)
					state->Failed.store(id, std::memory_order_release);
			}
		});
		if (!portals.empty())
			state->Buffers[9]->SetData(commands, portals.data(), static_cast<uint32_t>(portals.size_bytes()));
		for (size_t i = 0; i < inputs.size(); ++i)
		{
			const auto& input = inputs[i];
			const auto& request = input.Request;
			state->Buffers[3]->SetData(commands, &request, sizeof(request));
			if (!input.Nodes.empty())
				state->Buffers[4]->SetData(commands, input.Nodes.data(),
					static_cast<uint32_t>(input.Nodes.size() * sizeof(Soundwave::GpuOpeningNode)));
			if (!input.Reflections.empty())
				state->Buffers[10]->SetData(commands, input.Reflections.data(),
					static_cast<uint32_t>(input.Reflections.size() * sizeof(Soundwave::GpuReflection)));
			const uint32_t width = request.Config[0] + 2;
			const std::array groups{
				(width + 127) / 128, (width * width + 127) / 128, (request.Reflections[0] + 127) / 128, 1u};
			for (size_t stage = 0; stage < groups.size(); ++stage)
			{
				if (groups[stage] == 0)
					continue;
				Renderer::BeginComputePass(commands, state->Passes[stage]);
				Renderer::DispatchCompute(commands, state->Passes[stage], nullptr, {groups[stage], 1, 1});
				Renderer::EndComputePass(commands, state->Passes[stage]);
			}
			Renderer::Submit([state, commands, output = state->Buffers[6], indices = state->Buffers[12],
								 layout = layouts[i], id]() mutable {
				if (state->Failed.load(std::memory_order_relaxed) == id)
					return;
				commands->RT_RequireBufferState(output->GetBuffer().GetTracked(), ResourceState::CopySource);
				commands->RT_RequireBufferState(indices->GetBuffer().GetTracked(), ResourceState::CopySource);
				auto* transfer = commands->RT_BeginNRITransfer();
				if (!transfer)
				{
					state->Failed.store(id, std::memory_order_release);
					return;
				}
				RHIDevice::API().CmdCopyBuffer(*transfer, *state->Readback.Get(), layout.Offset,
					*output->GetBuffer().Get(), 0, layout.OutputBytes);
				if (layout.PathSlots)
					RHIDevice::API().CmdCopyBuffer(*transfer, *state->Readback.Get(),
						layout.Offset + layout.OutputBytes, *indices->GetBuffer().Get(), 0, layout.PathSlots * 4);
				commands->RT_EndNRISegment();
				state->Recorded = Renderer::RT_GetFrameNumber();
				state->RecordedSerial = id;
			});
		}
		commands->End();
		commands->Submit();
		// Frame retirement follows the graphics timeline. Join compute there so staging,
		// readback and scene teardown retain the same frame-fence lifetime guarantee.
		Renderer::Submit([commands]() mutable {
			Renderer::QueueWaitForCommandList(
				GPUQueue::Graphics, commands->GetQueue(), commands->GetLastExecutionInstance());
		});
		state->Completion->Begin();
		state->Completion->End();
		state->Completion->Submit();
		return true;
	}

	AudioAcoustics::ReverbBackend::PollResult AudioAcousticKernel::PollSources(std::vector<SourceOutput>& results)
	{
		auto state = m_Sources;
		if (!state || !state->Serial || state->Failed.load(std::memory_order_acquire) == state->Serial)
			return PollResult::Failed;
		if (state->Ready.load(std::memory_order_acquire) == state->Serial)
		{
			try
			{
				results = state->Results;
			}
			catch (const std::bad_alloc&)
			{
				return PollResult::Failed;
			}
			return PollResult::Ready;
		}
		if (state->PollQueued.exchange(true))
			return PollResult::Pending;
		Renderer::Submit([state, id = state->Serial, layouts = state->Layouts, bytes = state->ReadbackBytes]() mutable {
			state->PollQueued.store(false);
			if (state->RecordedSerial != id || state->Ready.load(std::memory_order_relaxed) == id ||
				Renderer::RT_GetFrameNumber() < state->Recorded + Renderer::GetConfig().FramesInFlight)
				return;
			const auto* mapped = static_cast<const std::byte*>(state->Readback.Map(0, bytes));
			if (!mapped)
			{
				state->Failed.store(id, std::memory_order_release);
				return;
			}
			bool valid = true;
			try
			{
				state->Results.clear();
				for (const auto layout : layouts)
				{
					SourceOutput output;
					std::memcpy(&output.Result, mapped + layout.Offset, sizeof(output.Result));
					if (output.Result.Paths > layout.PathSlots ||
						output.Result.Reflections > layout.PathSlots - output.Result.Paths)
					{
						valid = false;
						break;
					}
					output.Paths.resize(output.Result.Paths + 1);
					output.Reflections.resize(output.Result.Reflections);
					std::memcpy(output.Paths.data(), mapped + layout.Offset + sizeof(output.Result),
						output.Paths.size() * sizeof(Soundwave::GpuOpeningPath));
					if (!output.Reflections.empty())
						std::memcpy(output.Reflections.data(), mapped + layout.Offset + layout.OutputBytes,
							output.Reflections.size() * 4);
					state->Results.push_back(std::move(output));
				}
			}
			catch (const std::bad_alloc&)
			{
				valid = false;
			}
			state->Readback.Unmap();
			if (valid)
				state->Ready.store(id, std::memory_order_release);
			else
				state->Failed.store(id, std::memory_order_release);
		});
		return PollResult::Pending;
	}
}
