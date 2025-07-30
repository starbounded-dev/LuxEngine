#include "sepch.h"
#include "SceneSerializer.h"

#define GLM_ENABLE_EXPERIMENTAL

#include "Entity.h"
#include "Components.h"
#include "StarEngine/Scripting/ScriptEngine.h"
#include "StarEngine/Core/UUID.h"

#include "StarEngine/Project/Project.h"

#include <fstream>

#include <magic_enum.hpp>

#include <yaml-cpp/yaml.h>

#include "StarEngine/Core/Hash.h"

namespace YAML {

	template<>
	struct convert<glm::vec2>
	{
		static Node encode(const glm::vec2& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec2& rhs)
		{
			if (!node.IsSequence() || node.size() != 2)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec3>
	{
		static Node encode(const glm::vec3& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& rhs)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec4>
	{
		static Node encode(const glm::vec4& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.push_back(rhs.w);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec4& rhs)
		{
			if (!node.IsSequence() || node.size() != 4)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			rhs.w = node[3].as<float>();
			return true;
		
		}
	};

	template<>
	struct convert<StarEngine::UUID>
	{
		static Node encode(const StarEngine::UUID& uuid)
		{
			Node node;
			node.push_back((uint64_t)uuid);
			return node;
		}

		static bool decode(const Node& node, StarEngine::UUID& uuid)
		{
			uuid = node.as<uint64_t>();
			return true;
		}
	};

}

namespace StarEngine {

	#define WRITE_SCRIPT_FIELD(FieldType, Type)           \
				case ScriptFieldType::FieldType:          \
					out << scriptField.GetValue<Type>();  \
					break

	#define READ_SCRIPT_FIELD(FieldType, Type)             \
		case ScriptFieldType::FieldType:                   \
		{                                                  \
			Type data = scriptField["Data"].as<Type>();    \
			fieldInstance.SetValue(data);                  \
			break;                                         \
		}

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
		return out;
	}

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
		return out;
	}

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
		return out;
	}

	template<typename T>
	inline T TrySetEnum(T& value, const YAML::Node& node)
	{
		if (node)
			value = static_cast<T>(node.as<int>(static_cast<int>(value)));
		return value;
	}

	static std::string RigidBody2DBodyTypeToString(RigidBody2DComponent::BodyType type)
	{
		switch (type)
		{
			case RigidBody2DComponent::BodyType::Static: return "Static";
			case RigidBody2DComponent::BodyType::Dynamic: return "Dynamic";
			case RigidBody2DComponent::BodyType::Kinematic: return "Kinematic";
		}

		SE_CORE_ASSERT(false, "Unknown RigidBody2DComponent::BodyType!");
		return "";
	}

	static RigidBody2DComponent::BodyType RigidBody2DBodyTypeFromString(const std::string& type)
	{
		if (type == "Static") return RigidBody2DComponent::BodyType::Static;
		if (type == "Dynamic") return RigidBody2DComponent::BodyType::Dynamic;
		if (type == "Kinematic") return RigidBody2DComponent::BodyType::Kinematic;

		SE_CORE_ASSERT(false, "Unknown RigidBody2DComponent::BodyType!");
		return RigidBody2DComponent::BodyType::Static;
	}

	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{

	}

	void SerializeEntity(YAML::Emitter& out, Entity entity, Ref<Scene> scene)
	{
		SE_CORE_ASSERT(entity.HasComponent<IDComponent>());

		out << YAML::BeginMap; // Entity
		out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

		if (entity.HasComponent<TagComponent>())
		{
			out << YAML::Key << "TagComponent";
			out << YAML::BeginMap; // TagComponent

			auto& tag = entity.GetComponent<TagComponent>().Tag;
			out << YAML::Key << "Tag" << YAML::Value << tag;

			out << YAML::EndMap; // TagComponent
		}

		if (entity.HasComponent<TransformComponent>())
		{
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap; // TransformComponent

			auto& tc = entity.GetComponent<TransformComponent>();
			out << YAML::Key << "Translation" << YAML::Value << tc.Translation;
			out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
			out << YAML::Key << "Scale" << YAML::Value << tc.Scale;

			out << YAML::EndMap; // TransformComponent
		}

		if (entity.HasComponent<CameraComponent>())
		{
			out << YAML::Key << "CameraComponent";
			out << YAML::BeginMap; // CameraComponent

			auto& cameraComponent = entity.GetComponent<CameraComponent>();
			auto& camera = cameraComponent.Camera;

			out << YAML::Key << "Camera" << YAML::Value;
			out << YAML::BeginMap; // Camera
			out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera->GetProjectionType();
			out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera->GetPerspectiveVerticalFOV();
			out << YAML::Key << "PerspectiveNear" << YAML::Value << camera->GetPerspectiveNearClip();
			out << YAML::Key << "PerspectiveFar" << YAML::Value << camera->GetPerspectiveFarClip();
			out << YAML::Key << "OrthographicSize" << YAML::Value << camera->GetOrthographicSize();
			out << YAML::Key << "OrthographicNear" << YAML::Value << camera->GetOrthographicNearClip();
			out << YAML::Key << "OrthographicFar" << YAML::Value << camera->GetOrthographicFarClip();
			out << YAML::EndMap; // Camera

			out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
			out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;

			out << YAML::EndMap; // CameraComponent
		}

		if (entity.HasComponent<ScriptComponent>())
		{
			auto& scriptComponent = entity.GetComponent<ScriptComponent>();

			out << YAML::Key << "ScriptComponent";
			out << YAML::BeginMap; // ScriptComponent

			const auto& scriptEngine = ScriptEngine::GetInstance();
			const auto& sc = entity.GetComponent<ScriptComponent>();

			if (scriptEngine.IsValidScript(sc.ScriptHandle))
			{
				const auto& scriptMetadata = scriptEngine.GetScriptMetadata(sc.ScriptHandle);
				const auto& entityStorage = scene->GetScriptStorage().EntityStorage.at(entity.GetUUID());

				out << YAML::Key << "ScriptHandle" << YAML::Value << sc.ScriptHandle;
				out << YAML::Key << "ScriptName" << YAML::Value << scriptMetadata.FullName;

				out << YAML::Key << "Fields" << YAML::Value << YAML::BeginSeq;
				for (const auto& [fieldID, fieldStorage] : entityStorage.Fields)
				{
					const auto& fieldMetadata = scriptMetadata.Fields.at(fieldID);

					out << YAML::BeginMap;
					out << YAML::Key << "ID" << YAML::Value << fieldID;
					out << YAML::Key << "Name" << YAML::Value << fieldMetadata.Name;
					out << YAML::Key << "Type" << YAML::Value << std::string(magic_enum::enum_name(fieldMetadata.Type));
					out << YAML::Key << "Value" << YAML::Value;

					switch (fieldMetadata.Type)
					{
					case DataType::SByte:
						out << fieldStorage.GetValue<int8_t>();
						break;
					case DataType::Byte:
						out << fieldStorage.GetValue<uint8_t>();
						break;
					case DataType::Short:
						out << fieldStorage.GetValue<int16_t>();
						break;
					case DataType::UShort:
						out << fieldStorage.GetValue<uint16_t>();
						break;
					case DataType::Int:
						out << fieldStorage.GetValue<int32_t>();
						break;
					case DataType::UInt:
						out << fieldStorage.GetValue<uint32_t>();
						break;
					case DataType::Long:
						out << fieldStorage.GetValue<int64_t>();
						break;
					case DataType::ULong:
						out << fieldStorage.GetValue<uint64_t>();
						break;
					case DataType::Float:
						out << fieldStorage.GetValue<float>();
						break;
					case DataType::Double:
						out << fieldStorage.GetValue<double>();
						break;
					case DataType::Bool:
						//out << fieldStorage.GetValue<int16_t>();
						out << fieldStorage.GetValue<bool>();
						break;
					case DataType::String:
						out << fieldStorage.GetValue<Coral::String>();
						break;
					case DataType::Bool32:
						out << fieldStorage.GetValue<bool>();
						break;
					case DataType::AssetHandle:
						out << fieldStorage.GetValue<uint64_t>();
						break;
					case DataType::Vector2:
						out << fieldStorage.GetValue<glm::vec2>();
						break;
					case DataType::Vector3:
						out << fieldStorage.GetValue<glm::vec3>();
						break;
					case DataType::Vector4:
						out << fieldStorage.GetValue<glm::vec4>();
						break;
						//case DataType::Entity:
						//	out << fieldStorage.GetValue<uint64_t>();
						//	break;
						//case DataType::Prefab:
						//	out << fieldStorage.GetValue<uint64_t>();
						//	break;
						//case DataType::Mesh:
						//	out << fieldStorage.GetValue<uint64_t>();
						//	break;
						//case DataType::StaticMesh:
						//	out << fieldStorage.GetValue<uint64_t>();
						//	break;
						//case DataType::Material:
						//	out << fieldStorage.GetValue<uint64_t>();
						//	break;
						//case DataType::Texture2D:
						//	out << fieldStorage.GetValue<uint64_t>();
						//	break;
						//case DataType::Scene:
						//	out << fieldStorage.GetValue<uint64_t>();
						//	break;
					default:
						break;
					}

					out << YAML::EndMap;
				}
				out << YAML::EndSeq;
			}
			//
			//	out << YAML::Key << "ClassName" << YAML::Value << scriptComponent.ClassName;
			//
			//	// Fields
			//	Ref<ScriptClass> entityClass = ScriptEngine::GetEntityClass(scriptComponent.ClassName);
			//	const auto& fields = entityClass->GetFields();
			//	if (fields.size() > 0)
			//	{
			//		out << YAML::Key << "ScriptFields" << YAML::Value;
			//		auto& entityFields = ScriptEngine::GetScriptFieldMap(entity);
			//		out << YAML::BeginSeq;
			//		for (const auto& [name, field] : fields)
			//		{
			//			if (entityFields.find(name) == entityFields.end())
			//				continue;
			//
			//			out << YAML::BeginMap; // ScriptField
			//			out << YAML::Key << "Name" << YAML::Value << name;
			//			out << YAML::Key << "Type" << YAML::Value << Utils::ScriptFieldTypeToString(field.Type);
			//
			//			out << YAML::Key << "Data" << YAML::Value;
			//			ScriptFieldInstance& scriptField = entityFields.at(name);
			//
			//			switch (field.Type)
			//			{
			//			WRITE_SCRIPT_FIELD(Float, float);
			//			WRITE_SCRIPT_FIELD(Double, double);
			//			WRITE_SCRIPT_FIELD(Bool, bool);
			//			WRITE_SCRIPT_FIELD(SByte, int8_t);
			//			WRITE_SCRIPT_FIELD(Short, int16_t);
			//			WRITE_SCRIPT_FIELD(Int, int32_t);
			//			WRITE_SCRIPT_FIELD(Long, int64_t);
			//			WRITE_SCRIPT_FIELD(Byte, uint8_t);
			//			WRITE_SCRIPT_FIELD(UShort, uint16_t);
			//			WRITE_SCRIPT_FIELD(UInt, uint32_t);
			//			WRITE_SCRIPT_FIELD(ULong, uint64_t);
			//			WRITE_SCRIPT_FIELD(String, std::string);
			//			WRITE_SCRIPT_FIELD(Vector2, glm::vec2);
			//			WRITE_SCRIPT_FIELD(Vector3, glm::vec3);
			//			WRITE_SCRIPT_FIELD(Vector4, glm::vec4);
			//			WRITE_SCRIPT_FIELD(AssetHandle, AssetHandle);
			//			WRITE_SCRIPT_FIELD(Entity, UUID);
			//			}
			//			out << YAML::EndMap; // ScriptFields
			//		}
			//		out << YAML::EndSeq;
			//	}
			//
			out << YAML::EndMap; // ScriptComponent
		}

		if (entity.HasComponent<SpriteRendererComponent>())
		{
			out << YAML::Key << "SpriteRendererComponent";
			out << YAML::BeginMap; // SpriteRendererComponent

			auto& spriteRendererComponent = entity.GetComponent<SpriteRendererComponent>();
			out << YAML::Key << "Color" << YAML::Value << spriteRendererComponent.Color;

			out << YAML::Key << "TextureHandle" << YAML::Value << spriteRendererComponent.Texture;

			out << YAML::Key << "TilingFactor" << YAML::Value << spriteRendererComponent.TilingFactor;

			out << YAML::EndMap; // SpriteRendererComponent
		}

		if (entity.HasComponent<CircleRendererComponent>())
		{
			out << YAML::Key << "CircleRendererComponent";
			out << YAML::BeginMap; // CircleRendererComponent

			auto& circleRendererComponent = entity.GetComponent<CircleRendererComponent>();
			out << YAML::Key << "Color" << YAML::Value << circleRendererComponent.Color;
			out << YAML::Key << "Thickness" << YAML::Value << circleRendererComponent.Thickness;
			out << YAML::Key << "Fade" << YAML::Value << circleRendererComponent.Fade;

			out << YAML::EndMap; // CircleRendererComponent
		}

		if (entity.HasComponent<RigidBody2DComponent>())
		{
			out << YAML::Key << "RigidBody2DComponent";
			out << YAML::BeginMap; // RigidBody2DComponent

			auto& rb2dComponent = entity.GetComponent<RigidBody2DComponent>();
			out << YAML::Key << "BodyType" << YAML::Value << RigidBody2DBodyTypeToString(rb2dComponent.Type);
			out << YAML::Key << "FixedRotation" << YAML::Value << rb2dComponent.FixedRotation;

			out << YAML::EndMap; // RigidBody2DComponent
		}

		if (entity.HasComponent<BoxCollider2DComponent>())
		{
			out << YAML::Key << "BoxCollider2DComponent";
			out << YAML::BeginMap; // BoxCollider2DComponent

			auto& bc2dComponent = entity.GetComponent<BoxCollider2DComponent>();
			out << YAML::Key << "Offset" << YAML::Value << bc2dComponent.Offset;
			out << YAML::Key << "Size" << YAML::Value << bc2dComponent.Size;
			out << YAML::Key << "Density" << YAML::Value << bc2dComponent.Density;
			out << YAML::Key << "Friction" << YAML::Value << bc2dComponent.Friction;
			out << YAML::Key << "Restitution" << YAML::Value << bc2dComponent.Restitution;
			out << YAML::Key << "RestitutionThreshold" << YAML::Value << bc2dComponent.RestitutionThreshold;

			out << YAML::EndMap; // BoxCollider2DComponent
		}

		if (entity.HasComponent<CircleCollider2DComponent>())
		{
			out << YAML::Key << "CircleCollider2DComponent";
			out << YAML::BeginMap; // CircleCollider2DComponent

			auto& cc2dComponent = entity.GetComponent<CircleCollider2DComponent>();
			out << YAML::Key << "Offset" << YAML::Value << cc2dComponent.Offset;
			out << YAML::Key << "Radius" << YAML::Value << cc2dComponent.Radius;
			out << YAML::Key << "Density" << YAML::Value << cc2dComponent.Density;
			out << YAML::Key << "Friction" << YAML::Value << cc2dComponent.Friction;
			out << YAML::Key << "Restitution" << YAML::Value << cc2dComponent.Restitution;
			out << YAML::Key << "RestitutionThreshold" << YAML::Value << cc2dComponent.RestitutionThreshold;

			out << YAML::EndMap; // CircleCollider2DComponent
		}

		if (entity.HasComponent<TextComponent>())
		{
			out << YAML::Key << "TextComponent";
			out << YAML::BeginMap; // TextComponent

			auto& textComponent = entity.GetComponent<TextComponent>();
			out << YAML::Key << "TextString" << YAML::Value << textComponent.TextString;
			// TODO: textComponent.FontAsset
			out << YAML::Key << "Color" << YAML::Value << textComponent.Color;
			out << YAML::Key << "Kerning" << YAML::Value << textComponent.Kerning;
			out << YAML::Key << "LineSpacing" << YAML::Value << textComponent.LineSpacing;

			out << YAML::EndMap; // TextComponent
		}

		if (entity.HasComponent<AudioSourceComponent>())
		{
			out << YAML::Key << "AudioSourceComponent";
			out << YAML::BeginMap;

			const auto& audioSourceComponent = entity.GetComponent<AudioSourceComponent>();
			out << YAML::Key << "AudioHandle" << YAML::Value << audioSourceComponent.Audio;
			out << YAML::Key << "VolumeMultiplier" << YAML::Value << audioSourceComponent.Config.VolumeMultiplier;
			out << YAML::Key << "PitchMultiplier" << YAML::Value << audioSourceComponent.Config.PitchMultiplier;
			out << YAML::Key << "PlayOnAwake" << YAML::Value << audioSourceComponent.Config.PlayOnAwake;
			out << YAML::Key << "Looping" << YAML::Value << audioSourceComponent.Config.Looping;
			out << YAML::Key << "Spatialization" << YAML::Value << audioSourceComponent.Config.Spatialization;
			out << YAML::Key << "AttenuationModel" << YAML::Value << static_cast<int>(audioSourceComponent.Config.AttenuationModel);
			out << YAML::Key << "RollOff" << YAML::Value << audioSourceComponent.Config.RollOff;
			out << YAML::Key << "MinGain" << YAML::Value << audioSourceComponent.Config.MinGain;
			out << YAML::Key << "MaxGain" << YAML::Value << audioSourceComponent.Config.MaxGain;
			out << YAML::Key << "MinDistance" << YAML::Value << audioSourceComponent.Config.MinDistance;
			out << YAML::Key << "MaxDistance" << YAML::Value << audioSourceComponent.Config.MaxDistance;
			out << YAML::Key << "ConeInnerAngle" << YAML::Value << audioSourceComponent.Config.ConeInnerAngle;
			out << YAML::Key << "ConeOuterAngle" << YAML::Value << audioSourceComponent.Config.ConeOuterAngle;
			out << YAML::Key << "ConeOuterGain" << YAML::Value << audioSourceComponent.Config.ConeOuterGain;
			out << YAML::Key << "DopplerFactor" << YAML::Value << audioSourceComponent.Config.DopplerFactor;

			out << YAML::Key << "UsePlaylist" << YAML::Value << audioSourceComponent.AudioSourceData.UsePlaylist;

			if (audioSourceComponent.AudioSourceData.UsePlaylist)
			{
				out << YAML::Key << "AudioSourcesSize" << YAML::Value << audioSourceComponent.AudioSourceData.NumberOfAudioSources;
				out << YAML::Key << "StartIndex" << YAML::Value << audioSourceComponent.AudioSourceData.StartIndex;
				out << YAML::Key << "RepeatPlaylist" << YAML::Value << audioSourceComponent.AudioSourceData.RepeatPlaylist;
				out << YAML::Key << "RepeatSpecificTrack" << YAML::Value << audioSourceComponent.AudioSourceData.RepeatAfterSpecificTrackPlays;

				for (uint32_t i = 0; i < audioSourceComponent.AudioSourceData.Playlist.size(); i++)
				{
					if (audioSourceComponent.AudioSourceData.Playlist[i])
					{
						std::string audioName = "AudioHandle" + std::to_string(i);
						out << YAML::Key << audioName.c_str() << YAML::Value << audioSourceComponent.AudioSourceData.Playlist[i];
					}
				}
			}

			out << YAML::EndMap;
		}

		if (entity.HasComponent<AudioListenerComponent>())
		{
			out << YAML::Key << "AudioListenerComponent";
			out << YAML::BeginMap;

			const auto& audioListenerComponent = entity.GetComponent<AudioListenerComponent>();
			out << YAML::Key << "Active" << YAML::Value << audioListenerComponent.Active;
			out << YAML::Key << "ConeInnerAngle" << YAML::Value << audioListenerComponent.Config.ConeInnerAngle;
			out << YAML::Key << "ConeOuterAngle" << YAML::Value << audioListenerComponent.Config.ConeOuterAngle;
			out << YAML::Key << "ConeOuterGain" << YAML::Value << audioListenerComponent.Config.ConeOuterGain;

			out << YAML::EndMap;
		}

		out << YAML::EndMap; // Entity
	}

	void SceneSerializer::Serialize(const std::filesystem::path& filepath)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Untitled";
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

		auto view = m_Scene->m_Registry.view<IDComponent>();
		for (auto entityID : view)
		{
			Entity entity = { entityID, m_Scene.get() };
			if (!entity)
				continue;

			SerializeEntity(out, entity, m_Scene);
		}

		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	void SceneSerializer::SerializeRuntime(const std::filesystem::path& filepath)
	{
		// Not implemented
		SE_CORE_ASSERT(false);
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath.string());
		}
		catch (YAML::ParserException e)
		{
			SE_CORE_ERROR("Failed to load .starscene file '{0}'\n     {1}", filepath.string(), e.what());

			return false;
		}

		if (!data["Scene"])
			return false;

		std::string sceneName = data["Scene"].as<std::string>();
		SE_CORE_TRACE("Deserializing scene '{0}'", sceneName);

		auto entities = data["Entities"];
		if (entities)
		{
			for (auto entity : entities)
			{
				uint64_t uuid = entity["Entity"].as<uint64_t>();

				std::string name;
				auto tagComponent = entity["TagComponent"];
				if (tagComponent)
					name = tagComponent["Tag"].as<std::string>();

				SE_CORE_TRACE("Deserialized entity with ID = {0}, name = {1}", uuid, name);

				Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);

				auto transformComponent = entity["TransformComponent"];
				if (transformComponent)
				{
					// Entities always have transforms
					auto& tc = deserializedEntity.GetComponent<TransformComponent>();
					tc.Translation = transformComponent["Translation"].as<glm::vec3>();
					tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
					tc.Scale = transformComponent["Scale"].as<glm::vec3>();
				}

				auto cameraComponent = entity["CameraComponent"];
				if (cameraComponent)
				{
					auto& cc = deserializedEntity.AddComponent<CameraComponent>();
					cc.Camera = CreateRef<SceneCamera>();

					auto cameraProps = cameraComponent["Camera"];

					if (cameraProps["ProjectionType"])
						cc.Camera->SetProjectionType((SceneCamera::ProjectionType)cameraProps["ProjectionType"].as<int>());

					if (cameraProps["PerspectiveFOV"])
						cc.Camera->SetPerspectiveVerticalFOV(cameraProps["PerspectiveFOV"].as<float>());

					if (cameraProps["PerspectiveNear"])
						cc.Camera->SetPerspectiveNearClip(cameraProps["PerspectiveNear"].as<float>());

					if (cameraProps["PerspectiveFar"])
						cc.Camera->SetPerspectiveFarClip(cameraProps["PerspectiveFar"].as<float>());

					if (cameraProps["OrthographicSize"])
						cc.Camera->SetOrthographicSize(cameraProps["OrthographicSize"].as<float>());

					if (cameraProps["OrthographicNear"])
						cc.Camera->SetOrthographicNearClip(cameraProps["OrthographicNear"].as<float>());

					if (cameraProps["OrthographicFar"])
						cc.Camera->SetOrthographicFarClip(cameraProps["OrthographicFar"].as<float>());

					if (cameraComponent["Primary"])
						cc.Primary = cameraComponent["Primary"].as<bool>();

					if (cameraComponent["FixedAspectRatio"])
						cc.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
				}

				auto scriptComponent = entity["ScriptComponent"];
				if (scriptComponent)
				{
					uint64_t scriptID = scriptComponent["ScriptHandle"].as<uint64_t>(0);

					if (scriptID == 0)
					{
						scriptID = scriptComponent["ClassHandle"].as<uint64_t>(0);
						SE_CORE_VERIFY(scriptID == 0);
					}

					{
						auto& scriptEngine = ScriptEngine::GetMutable();

						if (scriptEngine.IsValidScript(scriptID))
						{
							const auto& scriptMetadata = scriptEngine.GetScriptMetadata(scriptID);

							ScriptComponent& sc = deserializedEntity.AddComponent<ScriptComponent>();
							sc.ScriptHandle = scriptID;

							m_Scene->m_ScriptStorage.InitializeEntityStorage(sc.ScriptHandle, deserializedEntity.GetUUID());

							bool oldFormat = false;

							auto fieldsArray = scriptComponent["Fields"];

							if (!fieldsArray)
							{
								fieldsArray = scriptComponent["StoredFields"];
								oldFormat = true;
							}

							for (auto field : fieldsArray)
							{
								uint32_t fieldID = field["ID"].as<uint32_t>(0);
								auto fieldName = field["Name"].as<std::string>("");

								if (oldFormat && fieldName.find(':') != std::string::npos)
								{
									// Old format, try generating id from name
									fieldName = fieldName.substr(fieldName.find(':') + 1);
									fieldID = Hash::GenerateFNVHash(fieldName);
								}

								if (scriptMetadata.Fields.find(fieldID) != scriptMetadata.Fields.end())
								{
									const auto& fieldMetadata = scriptMetadata.Fields.at(fieldID);
									auto& fieldStorage = m_Scene->m_ScriptStorage.EntityStorage.at(deserializedEntity.GetUUID()).Fields[fieldID];

									auto valueNode = oldFormat ? field["Data"] : field["Value"];

									switch (fieldMetadata.Type)
									{
									case DataType::SByte:
									{
										fieldStorage.SetValue(valueNode.as<int8_t>());
										break;
									}
									case DataType::Byte:
									{
										fieldStorage.SetValue(valueNode.as<uint8_t>());
										break;
									}
									case DataType::Short:
									{
										fieldStorage.SetValue(valueNode.as<int16_t>());
										break;
									}
									case DataType::UShort:
									{
										fieldStorage.SetValue(valueNode.as<uint16_t>());
										break;
									}
									case DataType::Int:
									{
										fieldStorage.SetValue(valueNode.as<int32_t>());
										break;
									}
									case DataType::UInt:
									{
										fieldStorage.SetValue(valueNode.as<uint32_t>());
										break;
									}
									case DataType::Long:
									{
										fieldStorage.SetValue(valueNode.as<int64_t>());
										break;
									}
									case DataType::ULong:
									{
										fieldStorage.SetValue(valueNode.as<uint64_t>());
										break;
									}
									case DataType::Float:
									{
										fieldStorage.SetValue(valueNode.as<float>());
										break;
									}
									case DataType::Double:
									{
										fieldStorage.SetValue(valueNode.as<double>());
										break;
									}
									case DataType::Bool:
									{
										fieldStorage.SetValue(valueNode.as<bool>());
										break;
									}
									case DataType::String:
									{
										fieldStorage.SetValue(Coral::String::New(valueNode.as<std::string>()));
										break;
									}
									case DataType::Bool32:
									{
										fieldStorage.SetValue((uint32_t)valueNode.as<bool>());
										break;
									}
									case DataType::AssetHandle:
									{
										fieldStorage.SetValue(valueNode.as<uint64_t>());
										break;
									}
									case DataType::Vector2:
									{
										fieldStorage.SetValue(valueNode.as<glm::vec2>());
										break;
									}
									case DataType::Vector3:
									{
										fieldStorage.SetValue(valueNode.as<glm::vec3>());
										break;
									}
									case DataType::Vector4:
									{
										fieldStorage.SetValue(valueNode.as<glm::vec4>());
										break;
									}
									//case DataType::Entity:
									//{
									//	fieldStorage.SetValue(valueNode.as<uint64_t>());
									//	break;
									//}
									//case DataType::Prefab:
									//{
									//	fieldStorage.SetValue(valueNode.as<uint64_t>());
									//	break;
									//}
									//case DataType::Mesh:
									//{
									//	fieldStorage.SetValue(valueNode.as<uint64_t>());
									//	break;
									//}
									//case DataType::StaticMesh:
									//{
									//	fieldStorage.SetValue(valueNode.as<uint64_t>());
									//	break;
									//}
									//case DataType::Material:
									//{
									//	fieldStorage.SetValue(valueNode.as<uint64_t>());
									//	break;
									//}
									//case DataType::Texture2D:
									//{
									//	fieldStorage.SetValue(valueNode.as<uint64_t>());
									//	break;
									//}
									//case DataType::Scene:
									//{
									//	fieldStorage.SetValue(valueNode.as<uint64_t>());
									//	break;
									//}
									default:
										break;
									}
								}
							}

							sc.HasInitializedScript = true;
						}
					}
				}

				auto spriteRendererComponent = entity["SpriteRendererComponent"];
				if (spriteRendererComponent)
				{
					auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
					src.Color = spriteRendererComponent["Color"].as<glm::vec4>();

					if (spriteRendererComponent["TexturePath"])
					{
						// NOTE: legacy, could try and find something in the asset registry that matches?
						// std::string texturePath = spriteRendererComponent["TexturePath"].as<std::string>();
						// auto path = Project::GetAssetFileSystemPath(texturePath);
						// src.Texture = Texture2D::Create(path.string());
					}

					if (spriteRendererComponent["TextureHandle"])
						src.Texture = spriteRendererComponent["TextureHandle"].as<AssetHandle>();

					if (spriteRendererComponent["TilingFactor"])
						src.TilingFactor = spriteRendererComponent["TilingFactor"].as<float>();
				}

				auto circleRendererComponent = entity["CircleRendererComponent"];
				if (circleRendererComponent)
				{
					auto& crc = deserializedEntity.AddComponent<CircleRendererComponent>();
					crc.Color = circleRendererComponent["Color"].as<glm::vec4>();
					crc.Thickness = circleRendererComponent["Thickness"].as<float>();
					crc.Fade = circleRendererComponent["Fade"].as<float>();
				}

				auto rigidBody2DComponent = entity["RigidBody2DComponent"];
				if (rigidBody2DComponent)
				{
					auto& rb2d = deserializedEntity.AddComponent<RigidBody2DComponent>();
					rb2d.Type = RigidBody2DBodyTypeFromString(rigidBody2DComponent["BodyType"].as<std::string>());
					rb2d.FixedRotation = rigidBody2DComponent["FixedRotation"].as<bool>();
				}

				auto boxCollider2DComponent = entity["BoxCollider2DComponent"];
				if (boxCollider2DComponent)
				{
					auto& bc2d = deserializedEntity.AddComponent<BoxCollider2DComponent>();
					bc2d.Offset = boxCollider2DComponent["Offset"].as<glm::vec2>();
					bc2d.Size = boxCollider2DComponent["Size"].as<glm::vec2>();
					bc2d.Density = boxCollider2DComponent["Density"].as<float>();
					bc2d.Friction = boxCollider2DComponent["Friction"].as<float>();
					bc2d.Restitution = boxCollider2DComponent["Restitution"].as<float>();
					bc2d.RestitutionThreshold = boxCollider2DComponent["RestitutionThreshold"].as<float>();
				}

				auto circleCollider2DComponent = entity["CircleCollider2DComponent"];
				if (circleCollider2DComponent)
				{
					auto& cc2d = deserializedEntity.AddComponent<CircleCollider2DComponent>();
					cc2d.Offset = circleCollider2DComponent["Offset"].as<glm::vec2>();
					cc2d.Radius = circleCollider2DComponent["Radius"].as<float>();
					cc2d.Density = circleCollider2DComponent["Density"].as<float>();
					cc2d.Friction = circleCollider2DComponent["Friction"].as<float>();
					cc2d.Restitution = circleCollider2DComponent["Restitution"].as<float>();
					cc2d.RestitutionThreshold = circleCollider2DComponent["RestitutionThreshold"].as<float>();
				}

				auto textComponent = entity["TextComponent"];
				if (textComponent)
				{
					auto& tc = deserializedEntity.AddComponent<TextComponent>();
					tc.TextString = textComponent["TextString"].as<std::string>();
					// tc.FontAsset // TODO
					tc.Color = textComponent["Color"].as<glm::vec4>();
					tc.Kerning = textComponent["Kerning"].as<float>();
					tc.LineSpacing = textComponent["LineSpacing"].as<float>();
				}

				auto audioSourceComponent = entity["AudioSourceComponent"];
				if (audioSourceComponent)
				{
					auto& component = deserializedEntity.AddComponent<AudioSourceComponent>();

					if (audioSourceComponent["AudioHandle"])
						component.Audio = audioSourceComponent["AudioHandle"].as<AssetHandle>();

					if (audioSourceComponent["VolumeMultiplier"])
						component.Config.VolumeMultiplier = audioSourceComponent["VolumeMultiplier"].as<float>();

					if (audioSourceComponent["PitchMultiplier"])
						component.Config.PitchMultiplier = audioSourceComponent["PitchMultiplier"].as<float>();

					if (audioSourceComponent["PlayOnAwake"])
						component.Config.PlayOnAwake = audioSourceComponent["PlayOnAwake"].as<bool>();

					if (audioSourceComponent["Looping"])
						component.Config.Looping = audioSourceComponent["Looping"].as<bool>();

					if (audioSourceComponent["Spatialization"])
						component.Config.Spatialization = audioSourceComponent["Spatialization"].as<bool>();

					TrySetEnum(component.Config.AttenuationModel, audioSourceComponent["AttenuationModel"]);

					if (audioSourceComponent["RollOff"])
						component.Config.RollOff = audioSourceComponent["RollOff"].as<float>();

					if (audioSourceComponent["MinGain"])
						component.Config.MinGain = audioSourceComponent["MinGain"].as<float>();

					if (audioSourceComponent["MaxGain"])
						component.Config.MaxGain = audioSourceComponent["MaxGain"].as<float>();

					if (audioSourceComponent["MinDistance"])
						component.Config.MinDistance = audioSourceComponent["MinDistance"].as<float>();

					if (audioSourceComponent["MaxDistance"])
						component.Config.MaxDistance = audioSourceComponent["MaxDistance"].as<float>();

					if (audioSourceComponent["ConeInnerAngle"])
						component.Config.ConeInnerAngle = audioSourceComponent["ConeInnerAngle"].as<float>();

					if (audioSourceComponent["ConeOuterAngle"])
						component.Config.ConeOuterAngle = audioSourceComponent["ConeOuterAngle"].as<float>();

					if (audioSourceComponent["ConeOuterGain"])
						component.Config.ConeOuterGain = audioSourceComponent["ConeOuterGain"].as<float>();

					if (audioSourceComponent["DopplerFactor"])
						component.Config.DopplerFactor = audioSourceComponent["DopplerFactor"].as<float>();

					if (component.Audio != 0)
					{
						Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(component.Audio);
						audioSource->SetConfig(component.Config);
					}

					if (audioSourceComponent["UsePlaylist"])
						component.AudioSourceData.UsePlaylist = audioSourceComponent["UsePlaylist"].as<bool>();

					if (component.AudioSourceData.UsePlaylist)
					{
						if (audioSourceComponent["AudioSourcesSize"])
							component.AudioSourceData.NumberOfAudioSources = audioSourceComponent["AudioSourcesSize"].as<int>();

						if (audioSourceComponent["StartIndex"])
							component.AudioSourceData.StartIndex = audioSourceComponent["StartIndex"].as<int>();

						if (audioSourceComponent["RepeatPlaylist"])
							component.AudioSourceData.RepeatPlaylist = audioSourceComponent["RepeatPlaylist"].as<bool>();

						if (audioSourceComponent["RepeatSpecificTrack"])
							component.AudioSourceData.RepeatAfterSpecificTrackPlays = audioSourceComponent["RepeatSpecificTrack"].as<bool>();

						for (uint32_t i = 0; i < component.AudioSourceData.NumberOfAudioSources; i++)
						{
							std::string audioName = "AudioHandle" + std::to_string(i);
							if (audioSourceComponent[audioName.c_str()])
							{
								AssetHandle audioHandle = audioSourceComponent[audioName.c_str()].as<AssetHandle>();
								component.AudioSourceData.Playlist.emplace_back(audioHandle);

							}
						}
					}
				}

				auto audioListenerComponent = entity["AudioListenerComponent"];
				if (audioListenerComponent)
				{
					auto& component = deserializedEntity.AddComponent<AudioListenerComponent>();

					if (audioListenerComponent["Active"])
						component.Active = audioListenerComponent["Active"].as<bool>();

					if (audioListenerComponent["ConeInnerAngle"])
						component.Config.ConeInnerAngle = audioListenerComponent["ConeInnerAngle"].as<float>();

					if (audioListenerComponent["ConeOuterAngle"])
						component.Config.ConeOuterAngle = audioListenerComponent["ConeOuterAngle"].as<float>();

					if (audioListenerComponent["ConeOuterGain"])
						component.Config.ConeOuterGain = audioListenerComponent["ConeOuterGain"].as<float>();
				}
			}
		}

		return true;
	}

	bool SceneSerializer::DeserializeRuntime(const std::filesystem::path& filepath)
	{
		// Not implemented
		SE_CORE_ASSERT(false);
		return false;
	}

}
