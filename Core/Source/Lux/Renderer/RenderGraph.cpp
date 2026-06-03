#include "lpch.h"
#include "RenderGraph.h"

#include <algorithm>
#include <format>

namespace Lux {

	void RenderGraph::Reset()
	{
		m_Textures.clear();
		m_Passes.clear();
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
		resources.erase(std::remove(resources.begin(), resources.end(), InvalidResource), resources.end());
		std::sort(resources.begin(), resources.end());
		resources.erase(std::unique(resources.begin(), resources.end()), resources.end());
	}

	std::vector<RenderGraph::ResourceLifetime> RenderGraph::BuildResourceLifetimes(std::vector<std::string>* diagnostics) const
	{
		std::vector<ResourceLifetime> lifetimes(m_Textures.size());
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
			lifetimes[resource].Resource = resource;

		for (uint32_t passIndex = 0; passIndex < m_Passes.size(); passIndex++)
		{
			const PassDesc& pass = m_Passes[passIndex];
			auto touchResource = [&](ResourceHandle resource)
				{
					if (resource >= lifetimes.size())
					{
						if (diagnostics)
							diagnostics->push_back(std::format("Pass '{}' references invalid resource {}", pass.Name, resource));
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
		result.Lifetimes = BuildResourceLifetimes(&result.Diagnostics);
		result.Valid = result.Diagnostics.empty();

		std::vector<bool> neededResources(m_Textures.size(), false);
		for (ResourceHandle resource = 0; resource < m_Textures.size(); resource++)
		{
			const TextureDesc& texture = m_Textures[resource];
			if (!texture.Transient)
				neededResources[resource] = true;
		}

		std::vector<bool> passNeeded(m_Passes.size(), false);
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
			passNeeded[index] = keepPass;
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
			if (passNeeded[passIndex])
				result.ExecutionOrder.push_back(passIndex);
			else
				result.CulledPasses.push_back(passIndex);
		}

		return result;
	}

	RenderGraph::CompileResult RenderGraph::Execute() const
	{
		CompileResult result = Compile();
		Execute(result);
		return result;
	}

	void RenderGraph::Execute(const CompileResult& compileResult) const
	{
		for (uint32_t passIndex : compileResult.ExecutionOrder)
		{
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

}
