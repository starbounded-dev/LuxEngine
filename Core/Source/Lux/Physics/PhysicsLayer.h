#pragma once

#include <string>
#include <vector>

namespace Lux {

	struct PhysicsLayer
	{
		uint32_t LayerID = 0;
		std::string Name;
		uint32_t BitValue = 1;
		uint32_t CollidesWith = 1;
		bool CollidesWithSelf = true;

		bool IsValid() const { return !Name.empty() && BitValue != 0; }
	};

	class PhysicsLayerManager
	{
	public:
		static void SetLayers(std::vector<PhysicsLayer> layers);
		static const std::vector<PhysicsLayer>& GetLayers();
		static PhysicsLayer& GetLayer(uint32_t layerID);
		static bool ShouldCollide(uint32_t layerA, uint32_t layerB);
		static void ClearLayers();

	private:
		static std::vector<PhysicsLayer> s_Layers;
		static PhysicsLayer s_NullLayer;
	};

}
