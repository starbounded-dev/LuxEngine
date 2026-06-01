#include "lpch.h"
#include "PhysicsLayer.h"

namespace Lux {

	std::vector<PhysicsLayer> PhysicsLayerManager::s_Layers;
	PhysicsLayer PhysicsLayerManager::s_NullLayer = { 0, "Default", 1, 1, true };

	void PhysicsLayerManager::SetLayers(std::vector<PhysicsLayer> layers)
	{
		s_Layers = std::move(layers);
		if (s_Layers.empty())
			s_Layers.push_back(s_NullLayer);
	}

	const std::vector<PhysicsLayer>& PhysicsLayerManager::GetLayers()
	{
		if (s_Layers.empty())
			s_Layers.push_back(s_NullLayer);
		return s_Layers;
	}

	PhysicsLayer& PhysicsLayerManager::GetLayer(uint32_t layerID)
	{
		for (PhysicsLayer& layer : s_Layers)
		{
			if (layer.LayerID == layerID)
				return layer;
		}
		return s_NullLayer;
	}

	bool PhysicsLayerManager::ShouldCollide(uint32_t layerA, uint32_t layerB)
	{
		const PhysicsLayer& a = GetLayer(layerA);
		const PhysicsLayer& b = GetLayer(layerB);
		if (!a.IsValid() || !b.IsValid())
			return true;

		if (a.LayerID == b.LayerID)
			return a.CollidesWithSelf;

		return (a.CollidesWith & b.BitValue) != 0 || (b.CollidesWith & a.BitValue) != 0;
	}

	void PhysicsLayerManager::ClearLayers()
	{
		s_Layers.clear();
	}

}
