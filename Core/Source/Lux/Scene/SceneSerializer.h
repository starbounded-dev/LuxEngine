#pragma once

#include "Lux/Scene/Scene.h"
#include "Lux/Asset/AssetSerializer.h"
#include "Lux/Serialization/FileStream.h"

#include <map>
#include <string>
#include <vector>

namespace YAML
{
	class Emitter;
}

namespace Lux
{
	class SceneSerializer
	{
	public:
		SceneSerializer(const Ref<Scene>& scene);

		void Serialize(const std::filesystem::path& filepath);
		void SerializeToYAML(YAML::Emitter& out);
		std::string SerializeToString();   // SerializeToYAML wrapped as a string (round-trips with DeserializeFromYAML)
		bool DeserializeFromYAML(const std::string& yamlString);

		// Snapshot the scene as separable parts, for granular (per-entity) undo storage:
		//   outMeta       — the scene YAML with an empty Entities list (name + post-processing)
		//   return value  — each entity's YAML block, keyed by UUID
		// Both are produced by round-tripping the scene through YAML, so two calls on the same scene
		// state yield identical strings (used for diffing). Reassemble + load with
		// DeserializeFromSnapshots — restore always goes through the whole-scene deserialize path.
		std::map<UUID, std::string> SerializeEntitySnapshots(std::string& outMeta);
		bool DeserializeFromSnapshots(const std::string& meta, const std::vector<std::string>& entityBlocks);

		// Round-trip guard for the snapshot path the undo system relies on: build a small scene with a
		// hierarchy and components, split → reassemble → re-split, and confirm every entity's YAML and
		// the scene metadata are reproduced exactly. Returns true on success; appends human-readable
		// messages to `failures`. Surfaced in the Renderer Debugger's self-test section.
		static bool RunRoundTripSelfTests(std::vector<std::string>* failures = nullptr);
		void SerializeRuntime(const std::filesystem::path& filepath);

		bool Deserialize(const std::filesystem::path& filepath);
		bool DeserializeRuntime(const std::filesystem::path& filepath);
		bool SerializeToAssetPack(FileStreamWriter& stream, AssetSerializationInfo& outInfo);
		bool DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::SceneInfo& sceneInfo);
	private:
		Ref<Scene> m_Scene;
	};
}
