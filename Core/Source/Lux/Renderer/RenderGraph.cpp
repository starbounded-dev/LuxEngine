#include "lpch.h"
#include "RenderGraph.h"

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
			static_cast<uint32_t>(RenderGraph::PassFlags::NeverCull);

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

		static bool IsValidResource(RenderGraph::ResourceHandle resource, size_t resourceCount)
		{
			return resource != RenderGraph::InvalidResource && resource < resourceCount;
		}

		static const std::string& GetResourceName(const std::vector<RenderGraph::TextureDesc>& textures, RenderGraph::ResourceHandle resource)
		{
			static const std::string s_EmptyName;
			if (!IsValidResource(resource, textures.size()))
				return s_EmptyName;

			return textures[resource].Name;
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

	}

	void RenderGraph::Reset()
	{
		m_Textures.clear();
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

	uint32_t RenderGraph::AddPass(PassDesc desc)
	{
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
		std::vector<ResourceLifetime> lifetimes(m_Textures.size());
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
			lifetimes[resource].Resource = resource;

		for (uint32_t passIndex = 0; passIndex < m_Passes.size(); passIndex++)
		{
			const PassDesc& pass = m_Passes[passIndex];
			auto touchResource = [&](ResourceHandle resource)
				{
					if (!IsValidResource(resource, lifetimes.size()))
					{
						if (diagnostics)
						{
							AppendDiagnostic(*diagnostics,
								DiagnosticSeverity::Error,
								DiagnosticCode::InvalidResource,
								passIndex,
								pass.Name,
								resource,
								GetResourceName(m_Textures, resource),
								std::format("Pass '{}' references invalid render graph resource {}.", pass.Name, resource));
						}
						return;
					}

					ResourceLifetime& lifetime = lifetimes[resource];
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
		result.ResourceFirstWriter.assign(m_Textures.size(), InvalidPassIndex);
		result.ResourceLastReader.assign(m_Textures.size(), InvalidPassIndex);
		result.ResourceConsumers.assign(m_Textures.size(), {});

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

		std::unordered_map<std::string, uint32_t> passNames;
		passNames.reserve(m_Passes.size());
		std::vector<bool> resourceWritten(m_Textures.size(), false);
		for (uint32_t passIndex = 0; passIndex < m_Passes.size(); passIndex++)
		{
			const PassDesc& pass = m_Passes[passIndex];
			const std::string passName = pass.Name.empty() ? std::format("Pass {}", passIndex) : pass.Name;
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

			if (pass.Execute && pass.Reads.empty() && pass.Writes.empty())
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
			else if (!pass.Execute && pass.Reads.empty() && pass.Writes.empty())
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
				if (!IsValidResource(resource, m_Textures.size()))
					continue;

				result.ResourceConsumers[resource].push_back(passIndex);
				result.ResourceLastReader[resource] = passIndex;

				if (!resourceWritten[resource])
				{
					const TextureDesc& texture = m_Textures[resource];
					if (texture.Transient)
					{
						AppendDiagnostic(result.Diagnostics,
							DiagnosticSeverity::Error,
							DiagnosticCode::ReadBeforeWrite,
							passIndex,
							passName,
							resource,
							GetResourceName(m_Textures, resource),
							std::format("Pass '{}' reads transient resource '{}' before any graph pass writes it.", passName, texture.Name));
					}
					else
					{
						AppendDiagnostic(result.Diagnostics,
							DiagnosticSeverity::Info,
							DiagnosticCode::UnwrittenExternalRead,
							passIndex,
							passName,
							resource,
							GetResourceName(m_Textures, resource),
							std::format("Pass '{}' reads external resource '{}' that has no producer inside this graph.", passName, texture.Name));
					}
				}
			}

			for (ResourceHandle resource : pass.Reads)
			{
				if (!IsValidResource(resource, m_Textures.size()))
					continue;

				if (ContainsResource(pass.Writes, resource))
				{
					AppendDiagnostic(result.Diagnostics,
						DiagnosticSeverity::Warning,
						DiagnosticCode::ReadWriteSameResource,
						passIndex,
						passName,
						resource,
						GetResourceName(m_Textures, resource),
						std::format("Pass '{}' reads and writes resource '{}' in the same pass.", passName, m_Textures[resource].Name));
				}
			}

			for (ResourceHandle resource : pass.Writes)
			{
				if (!IsValidResource(resource, m_Textures.size()))
					continue;

				if (result.ResourceFirstWriter[resource] == InvalidPassIndex)
					result.ResourceFirstWriter[resource] = passIndex;
				resourceWritten[resource] = true;
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
					result.ResourceFirstWriter[resource] < m_Passes.size() ? m_Passes[result.ResourceFirstWriter[resource]].Name : std::string(),
					resource,
					GetResourceName(m_Textures, resource),
					std::format("Transient resource '{}' is written but never consumed by a later pass.", m_Textures[resource].Name));
			}
		}

		std::vector<bool> neededResources(m_Textures.size(), false);
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
		{
			const TextureDesc& texture = m_Textures[resource];
			if (!texture.Transient)
				neededResources[resource] = true;
		}

		for (uint32_t passIndex = static_cast<uint32_t>(m_Passes.size()); passIndex > 0; passIndex--)
		{
			const uint32_t index = passIndex - 1;
			const PassDesc& pass = m_Passes[index];

			const bool pinned = HasFlag(pass.Flags, PassFlags::SideEffect) || HasFlag(pass.Flags, PassFlags::NeverCull);
			bool writesNeededOutput = false;
			for (ResourceHandle resource : pass.Writes)
			{
				if (resource < neededResources.size() && neededResources[resource])
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
				if (resource < neededResources.size())
					neededResources[resource] = false;
			}

			for (ResourceHandle resource : pass.Reads)
			{
				if (resource < neededResources.size())
					neededResources[resource] = true;
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
					if (!IsValidResource(lhs, m_Textures.size()) || !IsValidResource(rhs, m_Textures.size()))
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
							GetResourceName(m_Textures, lhs),
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
							GetResourceName(m_Textures, lhs),
							std::format("Alias group {} contains incompatible resources '{}' and '{}'.", group.AliasIndex, m_Textures[lhs].Name, m_Textures[rhs].Name));
					}
				}
			}
		}

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
		Execute(compileResult, 0, compileResult.ExecutionOrder.size());
	}

	void RenderGraph::Execute(const CompileResult& compileResult, size_t beginIndex, size_t endIndex) const
	{
		endIndex = std::min(endIndex, compileResult.ExecutionOrder.size());
		for (size_t i = beginIndex; i < endIndex; i++)
		{
			uint32_t passIndex = compileResult.ExecutionOrder[i];
			if (passIndex >= m_Passes.size())
				continue;

			const PassDesc& pass = m_Passes[passIndex];
			if (pass.Execute)
				pass.Execute();
		}
	}

	std::vector<RenderGraph::ResourceLifetime> RenderGraph::BuildAliasPlan() const
	{
		std::vector<ResourceLifetime> lifetimes = BuildResourceLifetimes();

		std::vector<ResourceHandle> lifetimeOrder;
		lifetimeOrder.reserve(lifetimes.size());
		for (ResourceHandle resource = 0; resource < lifetimes.size(); resource++)
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

		return !failures || failures->empty();
	}

}
