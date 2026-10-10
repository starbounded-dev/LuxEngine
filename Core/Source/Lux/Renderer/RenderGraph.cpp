// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RenderGraph.h"

#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/Renderer.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace Lux {

	namespace {

		constexpr uint32_t InvalidPassIndex = UINT32_MAX;

		constexpr uint32_t KnownPassFlags =
			static_cast<uint32_t>(RenderGraph::PassFlags::Graphics) |
			static_cast<uint32_t>(RenderGraph::PassFlags::Compute) |
			static_cast<uint32_t>(RenderGraph::PassFlags::Transfer) |
			static_cast<uint32_t>(RenderGraph::PassFlags::SideEffect) |
			static_cast<uint32_t>(RenderGraph::PassFlags::NeverCull) |
			static_cast<uint32_t>(RenderGraph::PassFlags::UntrackedResources);

		static uint32_t CountQueueFlags(RenderGraph::PassFlags flags)
		{
			uint32_t count = 0;
			if (RenderGraph::HasFlag(flags, RenderGraph::PassFlags::Graphics))
				count++;
			if (RenderGraph::HasFlag(flags, RenderGraph::PassFlags::Compute))
				count++;
			if (RenderGraph::HasFlag(flags, RenderGraph::PassFlags::Transfer))
				count++;
			return count;
		}

		// Executable graphs skip Name to avoid per-frame allocations; DebugName is always set.
		static std::string GetPassName(const RenderGraph::PassDesc& pass, uint32_t passIndex)
		{
			if (!pass.Name.empty())
				return pass.Name;
			if (pass.DebugName)
				return pass.DebugName;
			return std::format("Pass {}", passIndex);
		}

		static void AppendDiagnostic(std::vector<RenderGraph::Diagnostic>& diagnostics,
			RenderGraph::DiagnosticSeverity severity,
			RenderGraph::DiagnosticCode code,
			uint32_t passIndex,
			const std::string& passName,
			RenderGraph::ResourceHandle resource,
			const std::string& resourceName,
			std::string message)
		{
			RenderGraph::Diagnostic diagnostic;
			diagnostic.Severity = severity;
			diagnostic.Code = code;
			diagnostic.PassIndex = passIndex;
			diagnostic.PassName = passName;
			diagnostic.Resource = resource;
			diagnostic.ResourceName = resourceName;
			diagnostic.Message = std::move(message);
			diagnostics.push_back(std::move(diagnostic));
		}

		static void CountDiagnostics(RenderGraph::CompileResult& result)
		{
			result.ErrorCount = 0;
			result.WarningCount = 0;
			result.InfoCount = 0;

			for (const RenderGraph::Diagnostic& diagnostic : result.Diagnostics)
			{
				switch (diagnostic.Severity)
				{
					case RenderGraph::DiagnosticSeverity::Error:
						result.ErrorCount++;
						break;
					case RenderGraph::DiagnosticSeverity::Warning:
						result.WarningCount++;
						break;
					case RenderGraph::DiagnosticSeverity::Info:
						result.InfoCount++;
						break;
				}
			}

			result.Valid = result.ErrorCount == 0;
		}

		static bool LifetimesOverlap(const RenderGraph::ResourceLifetime& lhs, const RenderGraph::ResourceLifetime& rhs)
		{
			if (lhs.FirstPass == UINT32_MAX || rhs.FirstPass == UINT32_MAX)
				return false;

			return lhs.FirstPass <= rhs.LastPass && rhs.FirstPass <= lhs.LastPass;
		}

		static bool ContainsResource(const std::vector<RenderGraph::ResourceHandle>& resources, RenderGraph::ResourceHandle resource)
		{
			return std::binary_search(resources.begin(), resources.end(), resource);
		}

		// A graph resource resolved for the render thread, which may run after the main thread has
		// already Reset() and rebuilt the graph: it holds its own references, never the graph's.
		struct ResolvedResource
		{
			Ref<Image2D> Image;
			Ref<StorageBufferSet> Set;
			Ref<StorageBuffer> Buffer;
			TextureSubresourceRange Range = AllSubresources;
			ResourceState State = ResourceState::Unknown;
			std::string Name;

			// Render thread: the backend object this frame uses (alias source and frame slot resolved).
			void* RT_GetHandle()
			{
				if (Image)
					return Image->GetHandle().Get();
				if (Set)
				{
					Ref<StorageBuffer> buffer = Set->RT_Get();
					return buffer ? buffer->GetHandle().Get() : nullptr;
				}
				return Buffer ? Buffer->GetHandle().Get() : nullptr;
			}
		};

#ifdef LUX_DEBUG
		struct GraphResourceList : public RefCounted
		{
			std::vector<ResolvedResource> Resources;
		};
#endif

	}

	void RenderGraph::Reset()
	{
		m_Textures.clear();
		m_Buffers.clear();
		m_Passes.clear();
		m_ExternalDiagnostics.clear();
	}

	void RenderGraph::AddDiagnostic(Diagnostic diagnostic)
	{
		m_ExternalDiagnostics.push_back(std::move(diagnostic));
	}

	RenderGraph::ResourceHandle RenderGraph::AddTransientTexture(const TextureDesc& desc)
	{
		m_Textures.push_back(desc);
		return static_cast<ResourceHandle>(m_Textures.size() - 1);
	}

	RenderGraph::ResourceHandle RenderGraph::AddExternalBuffer(const BufferDesc& desc)
	{
		LUX_CORE_VERIFY(m_Buffers.size() < BufferHandleBit, "Too many render graph buffers");
		m_Buffers.push_back(desc);
		return BufferHandleBit | static_cast<ResourceHandle>(m_Buffers.size() - 1);
	}

	uint32_t RenderGraph::AddPass(PassDesc desc)
	{
		for (const ResourceAccess& access : desc.Accesses)
		{
			if (AccessReads(access.Kind))
				desc.Reads.push_back(access.Resource);
			if (AccessWrites(access.Kind))
				desc.Writes.push_back(access.Resource);
		}

		NormalizeResourceList(desc.Reads);
		NormalizeResourceList(desc.Writes);

		if (desc.Name.empty())
			desc.Name = std::format("Pass {}", m_Passes.size());

		m_Passes.push_back(std::move(desc));
		return static_cast<uint32_t>(m_Passes.size() - 1);
	}

	uint32_t RenderGraph::AddPass(PassDesc desc, ExecuteCallback execute)
	{
		desc.Execute = std::move(execute);
		return AddPass(std::move(desc));
	}

	bool RenderGraph::AccessReads(AccessKind kind)
	{
		switch (kind)
		{
			case AccessKind::SampledRead:
			case AccessKind::StorageRead:
			case AccessKind::StorageReadWrite:
			case AccessKind::ColorReadWrite:
			case AccessKind::DepthReadWrite:
			case AccessKind::DepthReadOnly:
			case AccessKind::CopySource:
			case AccessKind::IndirectArgs:
			case AccessKind::UniformRead:
				return true;
			case AccessKind::StorageWrite:
			case AccessKind::ColorWrite:
			case AccessKind::DepthWrite:
			case AccessKind::CopyDest:
				return false;
		}
		return false;
	}

	bool RenderGraph::AccessWrites(AccessKind kind)
	{
		switch (kind)
		{
			case AccessKind::StorageWrite:
			case AccessKind::StorageReadWrite:
			case AccessKind::ColorWrite:
			case AccessKind::ColorReadWrite:
			case AccessKind::DepthWrite:
			case AccessKind::DepthReadWrite:
			case AccessKind::CopyDest:
				return true;
			case AccessKind::SampledRead:
			case AccessKind::StorageRead:
			case AccessKind::DepthReadOnly:
			case AccessKind::CopySource:
			case AccessKind::IndirectArgs:
			case AccessKind::UniformRead:
				return false;
		}
		return false;
	}

	// The states the explicit-barrier commit functions require for the same bindings, so a pass-entry
	// requirement never fights the requirements its draws and dispatches make.
	ResourceState RenderGraph::AccessState(AccessKind kind, bool isBuffer)
	{
		switch (kind)
		{
			case AccessKind::SampledRead:      return ResourceState::ShaderResource;
			// Lux binds read-only storage buffers as SRVs, storage images always as UAVs.
			case AccessKind::StorageRead:      return isBuffer ? ResourceState::ShaderResource : ResourceState::UnorderedAccess;
			case AccessKind::StorageWrite:
			case AccessKind::StorageReadWrite: return ResourceState::UnorderedAccess;
			case AccessKind::ColorWrite:
			case AccessKind::ColorReadWrite:   return ResourceState::RenderTarget;
			case AccessKind::DepthWrite:
			case AccessKind::DepthReadWrite:   return ResourceState::DepthWrite;
			case AccessKind::DepthReadOnly:    return ResourceState::DepthRead;
			case AccessKind::CopySource:       return ResourceState::CopySource;
			case AccessKind::CopyDest:         return ResourceState::CopyDest;
			case AccessKind::IndirectArgs:     return ResourceState::IndirectArgument;
			case AccessKind::UniformRead:      return ResourceState::ConstantBuffer;
		}
		return ResourceState::Unknown;
	}

	bool RenderGraph::IsValidResource(ResourceHandle resource) const
	{
		if (resource == InvalidResource)
			return false;
		if (IsBufferHandle(resource))
			return (resource & ~BufferHandleBit) < m_Buffers.size();
		return resource < m_Textures.size();
	}

	uint32_t RenderGraph::GetResourceSlot(ResourceHandle resource) const
	{
		if (IsBufferHandle(resource))
			return static_cast<uint32_t>(m_Textures.size()) + (resource & ~BufferHandleBit);
		return resource;
	}

	bool RenderGraph::IsTransient(ResourceHandle resource) const
	{
		return !IsBufferHandle(resource) && m_Textures[resource].Transient;
	}

	std::string RenderGraph::GetResourceName(ResourceHandle resource) const
	{
		if (!IsValidResource(resource))
			return {};
		if (IsBufferHandle(resource))
			return m_Buffers[resource & ~BufferHandleBit].Name;
		return m_Textures[resource].Name;
	}

	bool RenderGraph::AreAliasCompatible(const TextureDesc& lhs, const TextureDesc& rhs)
	{
		return lhs.Transient && rhs.Transient
			&& lhs.AllowAlias && rhs.AllowAlias
			&& lhs.Format == rhs.Format
			&& lhs.Usage == rhs.Usage
			&& lhs.Dimension == rhs.Dimension
			&& lhs.Width == rhs.Width
			&& lhs.Height == rhs.Height
			&& lhs.Mips == rhs.Mips
			&& lhs.Layers == rhs.Layers;
	}

	void RenderGraph::NormalizeResourceList(std::vector<ResourceHandle>& resources)
	{
		std::sort(resources.begin(), resources.end());
		resources.erase(std::unique(resources.begin(), resources.end()), resources.end());
	}

	std::vector<RenderGraph::ResourceLifetime> RenderGraph::BuildResourceLifetimes(std::vector<Diagnostic>* diagnostics) const
	{
		std::vector<ResourceLifetime> lifetimes(GetResourceCount());
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
			lifetimes[resource].Resource = resource;
		for (ResourceHandle buffer = 0; buffer < m_Buffers.size(); buffer++)
			lifetimes[m_Textures.size() + buffer].Resource = BufferHandleBit | buffer;

		for (uint32_t passIndex = 0; passIndex < m_Passes.size(); passIndex++)
		{
			const PassDesc& pass = m_Passes[passIndex];
			auto touchResource = [&](ResourceHandle resource)
				{
					if (!IsValidResource(resource))
					{
						if (diagnostics)
						{
							AppendDiagnostic(*diagnostics,
								DiagnosticSeverity::Error,
								DiagnosticCode::InvalidResource,
								passIndex,
								pass.Name,
								resource,
								GetResourceName(resource),
								std::format("Pass '{}' references invalid render graph resource {}.", pass.Name, resource));
						}
						return;
					}

					ResourceLifetime& lifetime = lifetimes[GetResourceSlot(resource)];
					lifetime.FirstPass = std::min(lifetime.FirstPass, passIndex);
					lifetime.LastPass = std::max(lifetime.LastPass, passIndex);
				};

			for (ResourceHandle resource : pass.Reads)
				touchResource(resource);
			for (ResourceHandle resource : pass.Writes)
				touchResource(resource);
		}

		return lifetimes;
	}

	RenderGraph::CompileResult RenderGraph::Compile() const
	{
		CompileResult result;
		result.Diagnostics = m_ExternalDiagnostics;
		result.Lifetimes = BuildResourceLifetimes(&result.Diagnostics);
		result.PassNeededMask.assign(m_Passes.size(), false);
		result.ResourceFirstWriter.assign(GetResourceCount(), InvalidPassIndex);
		result.ResourceLastReader.assign(GetResourceCount(), InvalidPassIndex);
		result.ResourceConsumers.assign(GetResourceCount(), {});

		std::unordered_map<std::string, uint32_t> textureNames;
		textureNames.reserve(m_Textures.size());
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
		{
			const TextureDesc& texture = m_Textures[resource];
			const std::string textureName = texture.Name.empty() ? std::format("Resource {}", resource) : texture.Name;

			if (!texture.Image || !texture.Image->IsValid())
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Warning,
					DiagnosticCode::NullTexture,
					InvalidPassIndex,
					{},
					resource,
					textureName,
					std::format("Render graph texture '{}' has no valid GPU image bound.", textureName));
			}

			if (texture.Format == ImageFormat::None || texture.Usage == ImageUsage::None || texture.Width == 0 || texture.Height == 0 || texture.Mips == 0 || texture.Layers == 0)
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Error,
					DiagnosticCode::InvalidTextureDesc,
					InvalidPassIndex,
					{},
					resource,
					textureName,
					std::format("Render graph texture '{}' has an invalid descriptor ({}x{}, mips {}, layers {}).", textureName, texture.Width, texture.Height, texture.Mips, texture.Layers));
			}

			if (!texture.Name.empty())
			{
				const auto [it, inserted] = textureNames.emplace(texture.Name, resource);
				if (!inserted)
				{
					AppendDiagnostic(result.Diagnostics,
						DiagnosticSeverity::Warning,
						DiagnosticCode::DuplicateTextureName,
						InvalidPassIndex,
						{},
						resource,
						textureName,
						std::format("Render graph texture name '{}' is duplicated by resources {} and {}.", texture.Name, it->second, resource));
				}
			}
		}

		std::unordered_map<std::string, uint32_t> bufferNames;
		bufferNames.reserve(m_Buffers.size());
		for (uint32_t index = 0; index < m_Buffers.size(); index++)
		{
			const BufferDesc& buffer = m_Buffers[index];
			const ResourceHandle resource = BufferHandleBit | index;
			const std::string bufferName = buffer.Name.empty() ? std::format("Buffer {}", index) : buffer.Name;

			if (!buffer.Set && !buffer.Buffer)
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Error,
					DiagnosticCode::NullBuffer,
					InvalidPassIndex,
					{},
					resource,
					bufferName,
					std::format("Render graph buffer '{}' has no StorageBufferSet or StorageBuffer.", bufferName));
			}

			if (!buffer.Name.empty())
			{
				const auto [it, inserted] = bufferNames.emplace(buffer.Name, index);
				if (!inserted)
				{
					AppendDiagnostic(result.Diagnostics,
						DiagnosticSeverity::Warning,
						DiagnosticCode::DuplicateTextureName,
						InvalidPassIndex,
						{},
						resource,
						bufferName,
						std::format("Render graph buffer name '{}' is duplicated by buffers {} and {}.", buffer.Name, it->second, index));
				}
			}
		}

		std::unordered_map<std::string, uint32_t> passNames;
		passNames.reserve(m_Passes.size());
		std::vector<bool> resourceWritten(GetResourceCount(), false);
		for (uint32_t passIndex = 0; passIndex < m_Passes.size(); passIndex++)
		{
			const PassDesc& pass = m_Passes[passIndex];
			const std::string passName = GetPassName(pass, passIndex);
			const uint32_t rawFlags = static_cast<uint32_t>(pass.Flags);
			const uint32_t queueCount = CountQueueFlags(pass.Flags);

			if ((rawFlags & ~KnownPassFlags) != 0 || queueCount > 1)
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Error,
					DiagnosticCode::InvalidPassFlags,
					passIndex,
					passName,
					InvalidResource,
					{},
					std::format("Render graph pass '{}' has invalid flags 0x{:X}.", passName, rawFlags));
			}

			if (pass.Execute && queueCount == 0)
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Error,
					DiagnosticCode::InvalidPassFlags,
					passIndex,
					passName,
					InvalidResource,
					{},
					std::format("Executable render graph pass '{}' does not declare a graphics, compute, or transfer queue.", passName));
			}

			const bool untracked = HasFlag(pass.Flags, PassFlags::UntrackedResources);
			if (pass.Execute && pass.Reads.empty() && pass.Writes.empty() && !untracked)
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Warning,
					DiagnosticCode::EmptyExecutablePass,
					passIndex,
					passName,
					InvalidResource,
					{},
					std::format("Executable render graph pass '{}' has no declared inputs or outputs.", passName));
			}
			else if (!pass.Execute && pass.Reads.empty() && pass.Writes.empty() && !untracked)
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Warning,
					DiagnosticCode::EmptyMetadataPass,
					passIndex,
					passName,
					InvalidResource,
					{},
					std::format("Render graph pass '{}' has no declared inputs or outputs.", passName));
			}

			if (!pass.Name.empty())
			{
				const auto [it, inserted] = passNames.emplace(pass.Name, passIndex);
				if (!inserted)
				{
					AppendDiagnostic(result.Diagnostics,
						DiagnosticSeverity::Warning,
						DiagnosticCode::DuplicatePassName,
						passIndex,
						passName,
						InvalidResource,
						{},
						std::format("Render graph pass name '{}' is duplicated by passes {} and {}.", pass.Name, it->second, passIndex));
				}
			}

			for (ResourceHandle resource : pass.Reads)
			{
				if (!IsValidResource(resource))
					continue;

				const uint32_t slot = GetResourceSlot(resource);
				result.ResourceConsumers[slot].push_back(passIndex);
				result.ResourceLastReader[slot] = passIndex;

				if (!resourceWritten[slot])
				{
					if (IsTransient(resource))
					{
						AppendDiagnostic(result.Diagnostics,
							DiagnosticSeverity::Error,
							DiagnosticCode::ReadBeforeWrite,
							passIndex,
							passName,
							resource,
							GetResourceName(resource),
							std::format("Pass '{}' reads transient resource '{}' before any graph pass writes it.", passName, GetResourceName(resource)));
					}
					else
					{
						AppendDiagnostic(result.Diagnostics,
							DiagnosticSeverity::Info,
							DiagnosticCode::UnwrittenExternalRead,
							passIndex,
							passName,
							resource,
							GetResourceName(resource),
							std::format("Pass '{}' reads external resource '{}' that has no producer inside this graph.", passName, GetResourceName(resource)));
					}
				}
			}

			for (ResourceHandle resource : pass.Reads)
			{
				if (!IsValidResource(resource))
					continue;

				// Read + write of one resource is a load-and-store (drawing on top of an
				// attachment, in-place compute) — valid, so recorded for the inspector only.
				if (ContainsResource(pass.Writes, resource))
				{
					AppendDiagnostic(result.Diagnostics,
						DiagnosticSeverity::Info,
						DiagnosticCode::ReadWriteSameResource,
						passIndex,
						passName,
						resource,
						GetResourceName(resource),
						std::format("Pass '{}' reads and writes resource '{}' (load and store).", passName, GetResourceName(resource)));
				}
			}

			for (ResourceHandle resource : pass.Writes)
			{
				if (!IsValidResource(resource))
					continue;

				const uint32_t slot = GetResourceSlot(resource);
				if (result.ResourceFirstWriter[slot] == InvalidPassIndex)
					result.ResourceFirstWriter[slot] = passIndex;
				resourceWritten[slot] = true;
			}
		}

		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
		{
			if (result.ResourceFirstWriter[resource] == InvalidPassIndex || !m_Textures[resource].Transient)
				continue;

			bool hasReaderAfterWrite = false;
			for (uint32_t consumer : result.ResourceConsumers[resource])
			{
				if (consumer > result.ResourceFirstWriter[resource])
				{
					hasReaderAfterWrite = true;
					break;
				}
			}

			if (!hasReaderAfterWrite)
			{
				AppendDiagnostic(result.Diagnostics,
					DiagnosticSeverity::Warning,
					DiagnosticCode::DeadWrite,
					result.ResourceFirstWriter[resource],
					result.ResourceFirstWriter[resource] < m_Passes.size() ? GetPassName(m_Passes[result.ResourceFirstWriter[resource]], result.ResourceFirstWriter[resource]) : std::string(),
					resource,
					GetResourceName(resource),
					std::format("Transient resource '{}' written by '{}' is never consumed by a later pass.", GetResourceName(resource),
						result.ResourceFirstWriter[resource] < m_Passes.size() ? GetPassName(m_Passes[result.ResourceFirstWriter[resource]], result.ResourceFirstWriter[resource]) : std::string("?")));
			}
		}

		// External resources (non-transient textures, every buffer) are needed by definition.
		std::vector<bool> neededResources(GetResourceCount(), true);
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
			neededResources[resource] = !m_Textures[resource].Transient;

		for (uint32_t passIndex = static_cast<uint32_t>(m_Passes.size()); passIndex > 0; passIndex--)
		{
			const uint32_t index = passIndex - 1;
			const PassDesc& pass = m_Passes[index];

			const bool pinned = HasFlag(pass.Flags, PassFlags::SideEffect) || HasFlag(pass.Flags, PassFlags::NeverCull);
			bool writesNeededOutput = false;
			for (ResourceHandle resource : pass.Writes)
			{
				if (IsValidResource(resource) && neededResources[GetResourceSlot(resource)])
				{
					writesNeededOutput = true;
					break;
				}
			}

			const bool keepPass = pinned || writesNeededOutput;
			result.PassNeededMask[index] = keepPass;
			if (!keepPass)
				continue;

			for (ResourceHandle resource : pass.Writes)
			{
				if (IsValidResource(resource))
					neededResources[GetResourceSlot(resource)] = false;
			}

			for (ResourceHandle resource : pass.Reads)
			{
				if (IsValidResource(resource))
					neededResources[GetResourceSlot(resource)] = true;
			}
		}

		result.ExecutionOrder.reserve(m_Passes.size());
		for (uint32_t passIndex = 0; passIndex < m_Passes.size(); passIndex++)
		{
			if (result.PassNeededMask[passIndex])
				result.ExecutionOrder.push_back(passIndex);
			else
				result.CulledPasses.push_back(passIndex);
		}

		std::vector<ResourceLifetime> aliasPlan = BuildAliasPlan();
		for (ResourceHandle resource = 0; resource < result.Lifetimes.size() && resource < aliasPlan.size(); resource++)
			result.Lifetimes[resource].AliasIndex = aliasPlan[resource].AliasIndex;

		std::unordered_map<uint32_t, size_t> aliasGroupLookup;
		for (const ResourceLifetime& lifetime : result.Lifetimes)
		{
			if (lifetime.AliasIndex == UINT32_MAX)
				continue;

			const auto [it, inserted] = aliasGroupLookup.emplace(lifetime.AliasIndex, result.AliasGroups.size());
			if (inserted)
			{
				AliasGroupSummary group;
				group.AliasIndex = lifetime.AliasIndex;
				result.AliasGroups.push_back(std::move(group));
			}

			result.AliasGroups[it->second].Resources.push_back(lifetime.Resource);
		}

		for (AliasGroupSummary& group : result.AliasGroups)
		{
			for (size_t i = 0; i < group.Resources.size(); i++)
			{
				for (size_t j = i + 1; j < group.Resources.size(); j++)
				{
					const ResourceHandle lhs = group.Resources[i];
					const ResourceHandle rhs = group.Resources[j];
					if (lhs >= m_Textures.size() || rhs >= m_Textures.size())
						continue;

					if (LifetimesOverlap(result.Lifetimes[lhs], result.Lifetimes[rhs]))
					{
						group.Compatible = false;
						AppendDiagnostic(result.Diagnostics,
							DiagnosticSeverity::Error,
							DiagnosticCode::AliasLifetimeConflict,
							InvalidPassIndex,
							{},
							lhs,
							GetResourceName(lhs),
							std::format("Alias group {} contains resources '{}' and '{}' with overlapping lifetimes.", group.AliasIndex, m_Textures[lhs].Name, m_Textures[rhs].Name));
					}

					if (!AreAliasCompatible(m_Textures[lhs], m_Textures[rhs]))
					{
						group.Compatible = false;
						AppendDiagnostic(result.Diagnostics,
							DiagnosticSeverity::Error,
							DiagnosticCode::AliasIncompatibleResource,
							InvalidPassIndex,
							{},
							lhs,
							GetResourceName(lhs),
							std::format("Alias group {} contains incompatible resources '{}' and '{}'.", group.AliasIndex, m_Textures[lhs].Name, m_Textures[rhs].Name));
					}
				}
			}
		}

		// Pass-entry requirements: every declared access of a resource the pass uses in one state.
		// A resource used in several states (a mip chain read and written in place) is left to the
		// pass's own transitions and its draws' and dispatches' requirements.
		result.EntryRequirementOffsets.reserve(m_Passes.size() + 1);
		for (const PassDesc& pass : m_Passes)
		{
			result.EntryRequirementOffsets.push_back(static_cast<uint32_t>(result.EntryRequirements.size()));
			for (const ResourceAccess& access : pass.Accesses)
			{
				if (!IsValidResource(access.Resource))
					continue;

				const ResourceState state = AccessState(access.Kind, IsBufferHandle(access.Resource));
				const bool singleState = std::none_of(pass.Accesses.begin(), pass.Accesses.end(), [&](const ResourceAccess& other)
					{
						return other.Resource == access.Resource && AccessState(other.Kind, IsBufferHandle(other.Resource)) != state;
					});
				if (singleState)
					result.EntryRequirements.push_back({ access.Resource, access.Range, state });
			}
		}
		result.EntryRequirementOffsets.push_back(static_cast<uint32_t>(result.EntryRequirements.size()));

		CountDiagnostics(result);
		return result;
	}

	uint64_t RenderGraph::ComputeStructureHash() const
	{
		// FNV-1a fold over everything Compile() reads. ExecutionOrder/Lifetimes/
		// AliasGroups depend only on pass topology (reads/writes/flags) and texture
		// metadata — NOT on live GPU handles — so an unchanged hash means the cached
		// CompileResult stays valid even if the underlying images were reallocated.
		uint64_t hash = 1469598103934665603ull;
		const auto fold = [&hash](uint64_t value)
			{
				hash ^= value;
				hash *= 1099511628211ull;
			};
		const auto foldString = [&](const std::string& s)
			{
				for (const char c : s)
					fold(static_cast<unsigned char>(c));
				fold(s.size());
			};

		fold(m_Textures.size());
		for (const TextureDesc& texture : m_Textures)
		{
			foldString(texture.Name);
			fold(static_cast<uint64_t>(texture.Format));
			fold(static_cast<uint64_t>(texture.Usage));
			fold(static_cast<uint64_t>(texture.Dimension));
			fold(texture.Width);
			fold(texture.Height);
			fold(texture.Mips);
			fold(texture.Layers);
			fold(texture.Transient ? 1u : 0u);
			fold(texture.AllowAlias ? 1u : 0u);
			// Validity flips drive null/invalid-texture diagnostics, so fold it too.
			fold((texture.Image && texture.Image->IsValid()) ? 1u : 0u);
		}

		// Buffer presence drives the NullBuffer diagnostic; which buffer object is bound is resolved
		// at Execute(), like texture images.
		fold(m_Buffers.size());
		for (const BufferDesc& buffer : m_Buffers)
		{
			foldString(buffer.Name);
			fold(buffer.Set ? 1u : 0u);
			fold(buffer.Buffer ? 1u : 0u);
		}

		fold(m_Passes.size());
		for (const PassDesc& pass : m_Passes)
		{
			foldString(pass.Name);
			fold(static_cast<uint64_t>(pass.Flags));
			fold(pass.Execute ? 1u : 0u);
			fold(pass.Reads.size());
			for (const ResourceHandle resource : pass.Reads)
				fold(resource);
			fold(pass.Writes.size());
			for (const ResourceHandle resource : pass.Writes)
				fold(resource);
			// Accesses decide the compiled pass-entry requirements.
			fold(pass.Accesses.size());
			for (const ResourceAccess& access : pass.Accesses)
			{
				fold(access.Resource);
				fold(static_cast<uint64_t>(access.Kind));
				fold(access.Range.BaseMip);
				fold(access.Range.MipCount);
				fold(access.Range.BaseLayer);
				fold(access.Range.LayerCount);
			}
		}

		fold(m_ExternalDiagnostics.size());
		return hash;
	}

	RenderGraph::CompileResult RenderGraph::Execute() const
	{
		CompileResult result = Compile();
		Execute(result);
		return result;
	}

	void RenderGraph::Execute(const CompileResult& compileResult) const
	{
		Execute(compileResult, nullptr);
	}

	void RenderGraph::Execute(const CompileResult& compileResult, const Ref<RenderCommandBuffer>& commandBuffer) const
	{
		// Entry requirements only batch the draws' and dispatches' own, which cover every access too.
		const bool requireAccesses = commandBuffer && compileResult.EntryRequirementOffsets.size() == m_Passes.size() + 1;

		auto resolve = [this](ResourceHandle resource) -> ResolvedResource
			{
				ResolvedResource resolved;
				if (!IsValidResource(resource))
					return resolved;
				if (IsBufferHandle(resource))
				{
					const BufferDesc& buffer = m_Buffers[resource & ~BufferHandleBit];
					resolved.Set = buffer.Set;
					resolved.Buffer = buffer.Buffer;
				}
				else
				{
					resolved.Image = m_Textures[resource].Image;
				}
				return resolved;
			};

#ifdef LUX_DEBUG
		// Every graph resource, so a requirement on one a pass did not declare can be told apart from
		// one on a resource outside the graph (material textures, environment maps).
		Ref<GraphResourceList> graphResources;
		if (requireAccesses)
		{
			graphResources = Ref<GraphResourceList>::Create();
			graphResources->Resources.reserve(GetResourceCount());
			for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
			{
				ResolvedResource& resolved = graphResources->Resources.emplace_back(resolve(resource));
				resolved.Name = GetResourceName(resource);
			}
			for (ResourceHandle buffer = 0; buffer < m_Buffers.size(); buffer++)
			{
				ResolvedResource& resolved = graphResources->Resources.emplace_back(resolve(BufferHandleBit | buffer));
				resolved.Name = GetResourceName(BufferHandleBit | buffer);
			}
		}
#endif

		for (uint32_t passIndex : compileResult.ExecutionOrder)
		{
			if (passIndex >= m_Passes.size())
				continue;

			const PassDesc& pass = m_Passes[passIndex];
			if (!pass.Execute)
				continue;

			if (requireAccesses)
			{
				const uint32_t begin = compileResult.EntryRequirementOffsets[passIndex];
				const uint32_t end = compileResult.EntryRequirementOffsets[passIndex + 1];
				if (begin < end)
				{
					std::vector<ResolvedResource> entries;
					entries.reserve(end - begin);
					for (uint32_t i = begin; i < end; i++)
					{
						const CompileResult::EntryRequirement& requirement = compileResult.EntryRequirements[i];
						ResolvedResource& entry = entries.emplace_back(resolve(requirement.Resource));
						entry.Range = requirement.Range;
						entry.State = requirement.State;
					}

					Ref<RenderCommandBuffer> cmd = commandBuffer;
					Renderer::Submit([cmd, entries = std::move(entries)]() mutable
						{
							for (ResolvedResource& entry : entries)
							{
								void* handle = entry.RT_GetHandle();
								if (!handle)
									continue;
								if (entry.Image)
									cmd->RT_RequireTextureState(static_cast<nvrhi::ITexture*>(handle), entry.Range, entry.State);
								else
									cmd->RT_RequireBufferState(static_cast<nvrhi::IBuffer*>(handle), entry.State);
							}
							cmd->RT_CommitBarriers();
						});
				}

#ifdef LUX_DEBUG
				Ref<RenderCommandBuffer> cmd = commandBuffer;
				Renderer::Submit([cmd]() mutable { cmd->RT_BeginRequirementLog(); });
#endif
			}

			pass.Execute();

#ifdef LUX_DEBUG
			if (requireAccesses)
			{
				std::vector<ResolvedResource> declared;
				declared.reserve(pass.Reads.size() + pass.Writes.size());
				for (ResourceHandle resource : pass.Reads)
					declared.push_back(resolve(resource));
				for (ResourceHandle resource : pass.Writes)
					declared.push_back(resolve(resource));

				Ref<RenderCommandBuffer> cmd = commandBuffer;
				Ref<RuntimeDiagnosticSink> sink = m_RuntimeDiagnostics;
				Renderer::Submit([cmd, sink, graphResources, declared = std::move(declared), passName = GetPassName(pass, passIndex), passIndex]() mutable
					{
						const std::vector<const void*> required = cmd->RT_EndRequirementLog();
						for (const void* handle : required)
						{
							const bool isDeclared = std::any_of(declared.begin(), declared.end(), [&](ResolvedResource& resource) { return resource.RT_GetHandle() == handle; });
							if (isDeclared)
								continue;

							auto graphResource = std::find_if(graphResources->Resources.begin(), graphResources->Resources.end(),
								[&](ResolvedResource& resource) { return resource.RT_GetHandle() == handle; });
							if (graphResource == graphResources->Resources.end())
								continue; // not a graph resource: outside the graph's model

							std::scoped_lock lock(sink->Mutex);
							const bool reported = std::any_of(sink->Diagnostics.begin(), sink->Diagnostics.end(), [&](const Diagnostic& diagnostic)
								{
									return diagnostic.PassName == passName && diagnostic.ResourceName == graphResource->Name;
								});
							if (reported)
								continue;

							Diagnostic diagnostic;
							diagnostic.Severity = DiagnosticSeverity::Info;
							diagnostic.Code = DiagnosticCode::UndeclaredAccess;
							diagnostic.PassIndex = passIndex;
							diagnostic.PassName = passName;
							diagnostic.ResourceName = graphResource->Name;
							diagnostic.Message = std::format("Pass '{}' uses render graph resource '{}' without declaring it.", passName, graphResource->Name);
							sink->Diagnostics.push_back(std::move(diagnostic));
						}
					});
			}
#endif
		}
	}

	std::vector<RenderGraph::Diagnostic> RenderGraph::GetRuntimeDiagnostics() const
	{
		std::scoped_lock lock(m_RuntimeDiagnostics->Mutex);
		return m_RuntimeDiagnostics->Diagnostics;
	}

	std::vector<RenderGraph::ResourceLifetime> RenderGraph::BuildAliasPlan() const
	{
		std::vector<ResourceLifetime> lifetimes = BuildResourceLifetimes();

		std::vector<ResourceHandle> lifetimeOrder;
		lifetimeOrder.reserve(lifetimes.size());
		// Textures only (slot == handle): buffers never alias.
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
		{
			if (lifetimes[resource].FirstPass != UINT32_MAX)
				lifetimeOrder.push_back(resource);
		}

		std::sort(lifetimeOrder.begin(), lifetimeOrder.end(), [&](ResourceHandle a, ResourceHandle b)
			{
				const ResourceLifetime& lhs = lifetimes[a];
				const ResourceLifetime& rhs = lifetimes[b];
				if (lhs.FirstPass != rhs.FirstPass)
					return lhs.FirstPass < rhs.FirstPass;
				return lhs.LastPass < rhs.LastPass;
			});

		struct AliasGroup
		{
			ResourceHandle Representative = InvalidResource;
			uint32_t LastUse = 0;
		};

		std::vector<AliasGroup> aliasGroups;
		for (ResourceHandle resource : lifetimeOrder)
		{
			ResourceLifetime& lifetime = lifetimes[resource];
			const TextureDesc& texture = m_Textures[resource];

			if (!texture.Transient || !texture.AllowAlias)
				continue;

			for (uint32_t aliasIndex = 0; aliasIndex < aliasGroups.size(); aliasIndex++)
			{
				AliasGroup& group = aliasGroups[aliasIndex];
				if (group.LastUse < lifetime.FirstPass && AreAliasCompatible(m_Textures[group.Representative], texture))
				{
					lifetime.AliasIndex = aliasIndex;
					group.LastUse = lifetime.LastPass;
					break;
				}
			}

			if (lifetime.AliasIndex == UINT32_MAX)
			{
				lifetime.AliasIndex = static_cast<uint32_t>(aliasGroups.size());
				aliasGroups.push_back({ resource, lifetime.LastPass });
			}
		}

		return lifetimes;
	}

	bool RenderGraph::RunValidationSelfTests(std::vector<std::string>* failures)
	{
		auto addFailure = [&](std::string failure)
			{
				if (failures)
					failures->push_back(std::move(failure));
			};

		auto makeTexture = [](const char* name, bool transient = true, bool allowAlias = true, uint32_t width = 64, uint32_t height = 64)
			{
				TextureDesc desc;
				desc.Name = name;
				desc.Width = width;
				desc.Height = height;
				desc.Transient = transient;
				desc.AllowAlias = allowAlias;
				return desc;
			};

		auto hasDiagnostic = [](const CompileResult& result, DiagnosticCode code)
			{
				return std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(), [&](const Diagnostic& diagnostic)
					{
						return diagnostic.Code == code;
					});
			};

		auto hasError = [](const CompileResult& result, DiagnosticCode code)
			{
				return std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(), [&](const Diagnostic& diagnostic)
					{
						return diagnostic.Code == code && diagnostic.Severity == DiagnosticSeverity::Error;
					});
			};

		// The fixtures never bind a GPU image, so every texture also carries a NullTexture
		// warning. A test asserting "no warnings" means none besides that one.
		auto countFixtureIndependentWarnings = [](const CompileResult& result)
			{
				return static_cast<uint32_t>(std::count_if(result.Diagnostics.begin(), result.Diagnostics.end(), [](const Diagnostic& diagnostic)
					{
						return diagnostic.Severity == DiagnosticSeverity::Warning && diagnostic.Code != DiagnosticCode::NullTexture;
					}));
			};

		{
			RenderGraph graph;
			const ResourceHandle intermediate = graph.AddTransientTexture(makeTexture("Intermediate"));
			const ResourceHandle output = graph.AddTransientTexture(makeTexture("Output", false, false));
			graph.AddPass({ "BasePass", {}, { intermediate }, PassFlags::Graphics });
			graph.AddPass({ "Composite", { intermediate }, { output }, PassFlags::Graphics });
			const CompileResult result = graph.Compile();
			if (result.ErrorCount != 0)
				addFailure(std::format("Valid linear graph expected 0 errors, found {}.", result.ErrorCount));
		}

		{
			RenderGraph graph;
			graph.AddPass({ "InvalidRead", { 64 }, {}, PassFlags::Graphics });
			const CompileResult result = graph.Compile();
			if (!hasError(result, DiagnosticCode::InvalidResource))
				addFailure("Invalid resource handle was not reported as an error.");
		}

		{
			RenderGraph graph;
			const ResourceHandle texture = graph.AddTransientTexture(makeTexture("Unwritten"));
			graph.AddPass({ "ReadBeforeWrite", { texture }, {}, PassFlags::Graphics });
			const CompileResult result = graph.Compile();
			if (!hasError(result, DiagnosticCode::ReadBeforeWrite))
				addFailure("Transient read-before-write was not reported as an error.");
		}

		{
			RenderGraph graph;
			const ResourceHandle nullTexture = graph.AddTransientTexture(makeTexture("NullTexture"));
			const ResourceHandle zeroTexture = graph.AddTransientTexture(makeTexture("ZeroTexture", true, true, 0, 64));
			graph.AddPass({ "WriteNull", {}, { nullTexture }, PassFlags::Graphics });
			graph.AddPass({ "WriteZero", {}, { zeroTexture }, PassFlags::Graphics });
			const CompileResult result = graph.Compile();
			if (!hasDiagnostic(result, DiagnosticCode::NullTexture))
				addFailure("Null texture was not reported.");
			if (!hasError(result, DiagnosticCode::InvalidTextureDesc))
				addFailure("Zero-sized texture was not reported as an error.");
		}

		{
			RenderGraph graph;
			const ResourceHandle unused = graph.AddTransientTexture(makeTexture("Unused"));
			graph.AddPass({ "UnusedWrite", {}, { unused }, PassFlags::Graphics });
			const CompileResult result = graph.Compile();
			if (!hasDiagnostic(result, DiagnosticCode::DeadWrite))
				addFailure("Unused transient write was not reported.");
			if (result.CulledPasses.size() != 1 || result.CulledPasses[0] != 0)
				addFailure("Unused write pass did not remain stably culled.");
		}

		{
			RenderGraph graph;
			const ResourceHandle a = graph.AddTransientTexture(makeTexture("AliasA"));
			const ResourceHandle b = graph.AddTransientTexture(makeTexture("AliasB"));
			const ResourceHandle output = graph.AddTransientTexture(makeTexture("Output", false, false));
			graph.AddPass({ "WriteA", {}, { a }, PassFlags::Graphics });
			graph.AddPass({ "ConsumeA", { a }, { output }, PassFlags::Graphics });
			graph.AddPass({ "WriteB", {}, { b }, PassFlags::Graphics });
			graph.AddPass({ "ConsumeB", { b }, { output }, PassFlags::Graphics });
			const CompileResult result = graph.Compile();
			if (result.Lifetimes.size() <= b || result.Lifetimes[a].AliasIndex == UINT32_MAX || result.Lifetimes[a].AliasIndex != result.Lifetimes[b].AliasIndex)
				addFailure("Compatible non-overlapping transient textures did not share an alias group.");
		}

		{
			RenderGraph graph;
			const ResourceHandle a = graph.AddTransientTexture(makeTexture("OverlapA"));
			const ResourceHandle b = graph.AddTransientTexture(makeTexture("OverlapB"));
			const ResourceHandle output = graph.AddTransientTexture(makeTexture("Output", false, false));
			graph.AddPass({ "WriteA", {}, { a }, PassFlags::Graphics });
			graph.AddPass({ "WriteBAndReadA", { a }, { b }, PassFlags::Graphics });
			graph.AddPass({ "ConsumeBoth", { a, b }, { output }, PassFlags::Graphics });
			const CompileResult result = graph.Compile();
			if (result.Lifetimes.size() <= b || result.Lifetimes[a].AliasIndex == result.Lifetimes[b].AliasIndex)
				addFailure("Overlapping transient lifetimes incorrectly shared an alias group.");
		}

		{
			RenderGraph graph;
			const ResourceHandle color = graph.AddTransientTexture(makeTexture("Color", false, false));
			graph.AddPass({ "Clear", {}, { color }, PassFlags::Graphics });
			graph.AddPass({ "DrawOnTop", { color }, { color }, PassFlags::Graphics });
			graph.AddPass({ "BuffersOnly", {}, {}, CombineFlags(PassFlags::Compute, PassFlags::UntrackedResources) });
			const CompileResult result = graph.Compile();
			const uint32_t warningCount = countFixtureIndependentWarnings(result);
			if (warningCount != 0 || result.ErrorCount != 0)
				addFailure(std::format("Load-and-store and untracked-resource passes expected no warnings, found {} warning(s) and {} error(s).", warningCount, result.ErrorCount));
			if (!hasDiagnostic(result, DiagnosticCode::ReadWriteSameResource))
				addFailure("Load-and-store access was not recorded for the inspector.");
		}

		// Buffers: lifetimes span their passes, they never alias, and as external resources a pass that
		// writes one is never culled. A buffer read before any graph write is an external input.
		{
			RenderGraph graph;
			const ResourceHandle buffer = graph.AddExternalBuffer({ "Buffer" });
			const ResourceHandle output = graph.AddTransientTexture(makeTexture("Output", false, false));
			graph.AddPass({ "ReadBuffer", {}, {}, PassFlags::Compute, {}, nullptr, { { buffer, AccessKind::StorageRead } } });
			graph.AddPass({ "WriteBuffer", {}, {}, PassFlags::Compute, {}, nullptr, { { buffer, AccessKind::StorageWrite } } });
			graph.AddPass({ "ConsumeBuffer", {}, {}, PassFlags::Graphics, {}, nullptr, { { buffer, AccessKind::StorageRead }, { output, AccessKind::ColorWrite } } });
			const CompileResult result = graph.Compile();

			const uint32_t slot = graph.GetResourceSlot(buffer);
			if (!IsBufferHandle(buffer) || slot != graph.GetTextures().size() || result.Lifetimes.size() <= slot)
				addFailure("A buffer did not get the slot after the textures.");
			else if (result.Lifetimes[slot].FirstPass != 0 || result.Lifetimes[slot].LastPass != 2 || result.Lifetimes[slot].AliasIndex != UINT32_MAX)
				addFailure("A buffer's lifetime did not span its passes, or it was given an alias group.");
			if (std::find(result.CulledPasses.begin(), result.CulledPasses.end(), 1u) != result.CulledPasses.end())
				addFailure("A pass writing an external buffer was culled.");
			if (hasError(result, DiagnosticCode::ReadBeforeWrite) || !hasDiagnostic(result, DiagnosticCode::UnwrittenExternalRead))
				addFailure("A buffer read before its graph writer was not reported as an external input.");
		}

		// Accesses add their resources to Reads/Writes by kind.
		{
			RenderGraph graph;
			const ResourceHandle color = graph.AddTransientTexture(makeTexture("Color", false, false));
			const ResourceHandle depth = graph.AddTransientTexture(makeTexture("Depth", false, false));
			const ResourceHandle input = graph.AddTransientTexture(makeTexture("Input", false, false));
			graph.AddPass({ "Draw", {}, {}, PassFlags::Graphics, {}, nullptr,
				{ { color, AccessKind::ColorReadWrite }, { depth, AccessKind::DepthWrite }, { input, AccessKind::SampledRead } } });
			const PassDesc& pass = graph.GetPasses()[0];
			const bool readsOk = pass.Reads == std::vector<ResourceHandle>{ color, input };
			const bool writesOk = pass.Writes == std::vector<ResourceHandle>{ color, depth };
			if (!readsOk || !writesOk)
				addFailure("Accesses were not turned into the expected Reads/Writes.");
		}

		// The structure hash changes with an access kind, so a cached compile is not reused.
		{
			auto build = [&](AccessKind kind)
				{
					RenderGraph graph;
					const ResourceHandle texture = graph.AddTransientTexture(makeTexture("Texture", false, false));
					graph.AddPass({ "Pass", {}, {}, PassFlags::Compute, {}, nullptr, { { texture, kind } } });
					return graph.ComputeStructureHash();
				};
			if (build(AccessKind::StorageWrite) == build(AccessKind::StorageReadWrite))
				addFailure("The structure hash ignored a change of access kind.");
		}

		// Pass-entry requirements cover resources used in one state; one used in several is left out.
		{
			RenderGraph graph;
			const ResourceHandle single = graph.AddTransientTexture(makeTexture("Single", false, false));
			const ResourceHandle mixed = graph.AddTransientTexture(makeTexture("Mixed", false, false));
			const ResourceHandle buffer = graph.AddExternalBuffer({ "Buffer" });
			graph.AddPass({ "Compute", {}, {}, PassFlags::Compute, {}, nullptr,
				{
					{ single, AccessKind::StorageWrite },
					{ mixed, AccessKind::StorageWrite },
					{ mixed, AccessKind::SampledRead },
					{ buffer, AccessKind::StorageRead },
				} });
			const CompileResult result = graph.Compile();

			auto findEntry = [&](ResourceHandle resource) -> const CompileResult::EntryRequirement*
				{
					for (const CompileResult::EntryRequirement& entry : result.EntryRequirements)
					{
						if (entry.Resource == resource)
							return &entry;
					}
					return nullptr;
				};
			const CompileResult::EntryRequirement* singleEntry = findEntry(single);
			const CompileResult::EntryRequirement* bufferEntry = findEntry(buffer);
			if (result.EntryRequirementOffsets.size() != 2 || !singleEntry || singleEntry->State != ResourceState::UnorderedAccess)
				addFailure("A single-state resource got no UnorderedAccess entry requirement.");
			if (findEntry(mixed))
				addFailure("A resource used in several states got an entry requirement.");
			if (!bufferEntry || bufferEntry->State != ResourceState::ShaderResource)
				addFailure("A read-only storage buffer was not required as ShaderResource.");
		}

		return !failures || failures->empty();
	}

}
