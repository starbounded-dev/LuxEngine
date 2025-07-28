#pragma once

#include <unordered_map>

#include "AssetTypes.h"

namespace StarEngine {

	inline static std::unordered_map<std::string, AssetType> s_AssetExtensionMap =
	{
		// StarEngine types
		{ ".sscene", AssetType::Scene },
		{ ".smesh", AssetType::Mesh },
		{ ".ssmesh", AssetType::StaticMesh },
		{ ".smaterial", AssetType::Material },
		{ ".sskel", AssetType::Skeleton },
		{ ".sanim", AssetType::Animation },
		{ ".sanimgraph", AssetType::AnimationGraph },
		{ ".sprefab", AssetType::Prefab },
		{ ".ssoundc", AssetType::SoundConfig },

		{ ".cs", AssetType::ScriptFile },

		// mesh/animation source
		{ ".fbx", AssetType::MeshSource },
		{ ".gltf", AssetType::MeshSource },
		{ ".glb", AssetType::MeshSource },
		{ ".obj", AssetType::MeshSource },
		{ ".dae", AssetType::MeshSource },

		// Textures
		{ ".png", AssetType::Texture },
		{ ".jpg", AssetType::Texture },
		{ ".jpeg", AssetType::Texture },
		{ ".hdr", AssetType::EnvMap },

		// Audio
		{ ".wav", AssetType::Audio },
		{ ".ogg", AssetType::Audio },

		// Fonts
		{ ".ttf", AssetType::Font },
		{ ".ttc", AssetType::Font },
		{ ".otf", AssetType::Font },
		
		// Mesh Collider
		{ ".smc", AssetType::MeshCollider },

		// Graphs
		{ ".sound_graph", AssetType::SoundGraphSound }
	};

}
