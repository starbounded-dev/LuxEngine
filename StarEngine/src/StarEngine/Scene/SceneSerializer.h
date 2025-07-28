#pragma once

#include "Scene.h"

#include "StarEngine/Serialization/FileStream.h"
#include "StarEngine/Asset/AssetSerializer.h"

namespace YAML {
	class Emitter;
	class Node;
}

namespace StarEngine {

	class SceneSerializer
	{
	public:
		SceneSerializer(const Ref<Scene>& scene);

		void Serialize(const std::filesystem::path& filepath);
		void SerializeToYAML(YAML::Emitter& out);
		bool DeserializeFromYAML(const std::string& yamlString);
		void SerializeRuntime(AssetHandle scene);

		bool Deserialize(const std::filesystem::path& filepath);
		bool DeserializeRuntime(AssetHandle scene);

		bool SerializeToAssetPack(FileStreamWriter& stream, AssetSerializationInfo& outInfo);
		bool DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::SceneInfo& sceneInfo);

		bool DeserializeReferencedPrefabs(const std::filesystem::path& filepath, std::unordered_set<AssetHandle>& outPrefabs);
	public:
		static void SerializeEntity(YAML::Emitter& out, Entity entity, Ref<Scene> scene);
		static void DeserializeEntities(YAML::Node& entitiesNode, Ref<Scene> scene);
	public:
		inline static std::string_view FileFilter = "Star Scene (*.sscene)\0*.sscene\0";
		inline static std::string_view DefaultExtension = ".sscene";

	private:
		Ref<Scene> m_Scene;
	};

}
