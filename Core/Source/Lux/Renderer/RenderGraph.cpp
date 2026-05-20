#include "lpch.h"
#include "RenderGraph.h"

#include <algorithm>

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

	uint32_t RenderGraph::AddPass(const PassDesc& desc)
	{
		m_Passes.push_back(desc);
		return static_cast<uint32_t>(m_Passes.size() - 1);
	}

	std::vector<RenderGraph::ResourceLifetime> RenderGraph::BuildAliasPlan() const
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
						return;

					ResourceLifetime& lifetime = lifetimes[resource];
					lifetime.FirstPass = std::min(lifetime.FirstPass, passIndex);
					lifetime.LastPass = std::max(lifetime.LastPass, passIndex);
				};

			for (ResourceHandle resource : pass.Reads)
				touchResource(resource);
			for (ResourceHandle resource : pass.Writes)
				touchResource(resource);
		}

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

		std::vector<uint32_t> aliasLastUse;
		for (ResourceHandle resource : lifetimeOrder)
		{
			ResourceLifetime& lifetime = lifetimes[resource];

			for (uint32_t aliasIndex = 0; aliasIndex < aliasLastUse.size(); aliasIndex++)
			{
				if (aliasLastUse[aliasIndex] < lifetime.FirstPass)
				{
					lifetime.AliasIndex = aliasIndex;
					aliasLastUse[aliasIndex] = lifetime.LastPass;
					break;
				}
			}

			if (lifetime.AliasIndex == UINT32_MAX)
			{
				lifetime.AliasIndex = static_cast<uint32_t>(aliasLastUse.size());
				aliasLastUse.push_back(lifetime.LastPass);
			}
		}

		return lifetimes;
	}

}
