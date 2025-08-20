#pragma once

#include "StarEngine/Scene/Scene.h"

namespace YAML {
	class Emitter;
	class Node;
}

namespace StarEngine
{
	class SceneSerializer
	{
	public:
		SceneSerializer(const Ref<Scene>& scene);

		void Serialize(const std::filesystem::path& filepath);
		void SerializeToYAML(YAML::Emitter& out);
		void SerializeRuntime(AssetHandle scene);

		bool Deserialize(const std::filesystem::path& filepath);
		bool DeserializeFromYAML(const std::string& yamlString);
		bool DeserializeRuntime(const std::filesystem::path& filepath);

		static void DeserializeEntities(YAML::Node& entitiesNode, Ref<Scene> scene);
	private:
		Ref<Scene> m_Scene;
	};
}
