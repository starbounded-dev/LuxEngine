#include "sepch.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "SceneSerializer.h"

#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Entity.h"
#include "Components.h"
#include "StarEngine/Scripting/ScriptEngine.h"
#include "StarEngine/Core/UUID.h"

#include "StarEngine/Project/Project.h"

#include <fstream>

#include <magic_enum.hpp>

#include <yaml-cpp/yaml.h>

#include "StarEngine/Core/Hash.h"
#include "StarEngine/Utilities/StringUtils.h"

namespace YAML {

	template<>
	struct convert<glm::quat>
	{
		static Node encode(const glm::quat& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.push_back(rhs.w);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::quat& rhs)
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

	static std::string RigidBody2DBodyTypeToString(RigidBody2DComponent::Type type)
	{
		switch (type)
		{
			case RigidBody2DComponent::Type::Static: return "Static";
			case RigidBody2DComponent::Type::Dynamic: return "Dynamic";
			case RigidBody2DComponent::Type::Kinematic: return "Kinematic";
		}

		SE_CORE_ASSERT(false, "Unknown RigidBody2DComponent::BodyType!");
		return "";
	}

	static RigidBody2DComponent::Type RigidBody2DBodyTypeFromString(const std::string& type)
	{
		if (type == "Static") return RigidBody2DComponent::Type::Static;
		if (type == "Dynamic") return RigidBody2DComponent::Type::Dynamic;
		if (type == "Kinematic") return RigidBody2DComponent::Type::Kinematic;

		SE_CORE_ASSERT(false, "Unknown RigidBody2DComponent::BodyType!");
		return RigidBody2DComponent::Type::Static;
	}

	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{

	}

	void SerializeEntity(YAML::Emitter& out, Entity entity, Ref<Scene> scene)
	{
		SE_CORE_ASSERT(entity.HasComponent<IDComponent>());

		UUID uuid = entity.GetComponent<IDComponent>().ID;
		out << YAML::BeginMap; // Entity
		out << YAML::Key << "Entity";
		out << YAML::Value << uuid;

		if (entity.HasComponent<TagComponent>())
		{
			out << YAML::Key << "TagComponent";
			out << YAML::BeginMap; // TagComponent

			auto& tag = entity.GetComponent<TagComponent>().Tag;
			out << YAML::Key << "Tag" << YAML::Value << tag;

			out << YAML::EndMap; // TagComponent
		}

		if (entity.HasComponent<RelationshipComponent>())
		{
			auto& relationshipComponent = entity.GetComponent<RelationshipComponent>();
			out << YAML::Key << "Parent" << YAML::Value << relationshipComponent.ParentHandle;

			out << YAML::Key << "Children";
			out << YAML::Value << YAML::BeginSeq;

			for (auto child : relationshipComponent.Children)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "Handle" << YAML::Value << child;
				out << YAML::EndMap;
			}
			out << YAML::EndSeq;
		}

		if (entity.HasComponent<TransformComponent>())
		{
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap; // TransformComponent

			auto& transform = entity.GetComponent<TransformComponent>();
			out << YAML::Key << "Position" << YAML::Value << transform.Translation;
			out << YAML::Key << "Rotation" << YAML::Value << transform.GetRotationEuler();
			out << YAML::Key << "Scale" << YAML::Value << transform.Scale;

			out << YAML::EndMap; // TransformComponent
		}

		if (entity.HasComponent<ScriptComponent>())
		{
			out << YAML::Key << "ScriptComponent";
			out << YAML::BeginMap; // ScriptComponent

			const auto& scriptEngine = ScriptEngine::GetInstance();
			const auto& sc = entity.GetComponent<ScriptComponent>();

			if (scriptEngine.IsValidScript(sc.ScriptID))
			{
				const auto& scriptMetadata = scriptEngine.GetScriptMetadata(sc.ScriptID);
				const auto& entityStorage = scene->GetScriptStorage().EntityStorage.at(entity.GetUUID());

				out << YAML::Key << "ScriptID" << YAML::Value << sc.ScriptID;
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

					if (fieldStorage.IsArray())
					{
						out << YAML::BeginSeq;

						for (int32_t i = 0; i < fieldStorage.GetLength(); i++)
						{
							switch (fieldMetadata.Type)
							{
							case DataType::Bool:
								out << fieldStorage.GetValue<bool>(i);
								break;
							case DataType::SByte:
								out << fieldStorage.GetValue<int8_t>(i);
								break;
							case DataType::Byte:
								out << fieldStorage.GetValue<uint8_t>(i);
								break;
							case DataType::Short:
								out << fieldStorage.GetValue<int16_t>(i);
								break;
							case DataType::UShort:
								out << fieldStorage.GetValue<uint16_t>(i);
								break;
							case DataType::Int:
								out << fieldStorage.GetValue<int32_t>(i);
								break;
							case DataType::UInt:
								out << fieldStorage.GetValue<uint32_t>(i);
								break;
							case DataType::Long:
								out << fieldStorage.GetValue<int64_t>(i);
								break;
							case DataType::ULong:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							case DataType::Float:
								out << fieldStorage.GetValue<float>(i);
								break;
							case DataType::Double:
								out << fieldStorage.GetValue<double>(i);
								break;
							case DataType::Vector2:
								out << fieldStorage.GetValue<glm::vec2>(i);
								break;
							case DataType::Vector3:
								out << fieldStorage.GetValue<glm::vec3>(i);
								break;
							case DataType::Vector4:
								out << fieldStorage.GetValue<glm::vec4>(i);
								break;
							case DataType::String:
								out << fieldStorage.GetValue<std::string>(i);
								break;
							case DataType::Entity:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							case DataType::Prefab:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							case DataType::Mesh:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							case DataType::StaticMesh:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							case DataType::Material:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							case DataType::Texture2D:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							case DataType::Scene:
								out << fieldStorage.GetValue<uint64_t>(i);
								break;
							default:
								break;
							}
						}

						out << YAML::EndSeq;
					}
					else
					{
						switch (fieldMetadata.Type)
						{
						case DataType::Bool:
							out << fieldStorage.GetValue<bool>();
							break;
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
						case DataType::Vector2:
							out << fieldStorage.GetValue<glm::vec2>();
							break;
						case DataType::Vector3:
							out << fieldStorage.GetValue<glm::vec2>();
							break;
						case DataType::Vector4:
							out << fieldStorage.GetValue<glm::vec2>();
							break;
							// TODO(Emily): This appears to write a spurious `\x00`
						case DataType::String:
							out << fieldStorage.GetValue<std::string>();
							break;
						case DataType::Entity:
							out << fieldStorage.GetValue<uint64_t>();
							break;
						case DataType::Prefab:
							out << fieldStorage.GetValue<uint64_t>();
							break;
						case DataType::Mesh:
							out << fieldStorage.GetValue<uint64_t>();
							break;
						case DataType::StaticMesh:
							out << fieldStorage.GetValue<uint64_t>();
							break;
						case DataType::Material:
							out << fieldStorage.GetValue<uint64_t>();
							break;
						case DataType::Texture2D:
							out << fieldStorage.GetValue<uint64_t>();
							break;
						case DataType::Scene:
							out << fieldStorage.GetValue<uint64_t>();
							break;
						default:
							break;
						}
					}

					out << YAML::EndMap;
				}
				out << YAML::EndSeq;
			}

			out << YAML::EndMap; // ScriptComponent
		}

		if (entity.HasComponent<CameraComponent>())
		{
			out << YAML::Key << "CameraComponent";
			out << YAML::BeginMap; // CameraComponent

			auto& cameraComponent = entity.GetComponent<CameraComponent>();
			auto& camera = cameraComponent.Camera;
			out << YAML::Key << "Camera" << YAML::Value;
			out << YAML::BeginMap; // Camera
			out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
			out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetDegPerspectiveVerticalFOV();
			out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
			out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
			out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
			out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
			out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
			out << YAML::EndMap; // Camera
			out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;

			out << YAML::EndMap; // CameraComponent
		}

		if (entity.HasComponent<SpriteRendererComponent>())
		{
			out << YAML::Key << "SpriteRendererComponent";
			out << YAML::BeginMap; // SpriteRendererComponent

			auto& spriteRendererComponent = entity.GetComponent<SpriteRendererComponent>();
			out << YAML::Key << "Color" << YAML::Value << spriteRendererComponent.Color;

			out << YAML::Key << "Texture" << YAML::Value << spriteRendererComponent.Texture;
			out << YAML::Key << "TilingFactor" << YAML::Value << spriteRendererComponent.TilingFactor;
			out << YAML::Key << "UVStart" << YAML::Value << spriteRendererComponent.UVStart;
			out << YAML::Key << "UVEnd" << YAML::Value << spriteRendererComponent.UVEnd;
			out << YAML::Key << "ScreenSpace" << YAML::Value << spriteRendererComponent.ScreenSpace;


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

		if (entity.HasComponent<TextComponent>())
		{
			out << YAML::Key << "TextComponent";
			out << YAML::BeginMap; // TextComponent

			auto& textComponent = entity.GetComponent<TextComponent>();
			out << YAML::Key << "TextString" << YAML::Value << textComponent.TextString;
			out << YAML::Key << "FontHandle" << YAML::Value << textComponent.FontHandle;
			out << YAML::Key << "Color" << YAML::Value << textComponent.Color;
			out << YAML::Key << "LineSpacing" << YAML::Value << textComponent.LineSpacing;
			out << YAML::Key << "Kerning" << YAML::Value << textComponent.Kerning;
			out << YAML::Key << "MaxWidth" << YAML::Value << textComponent.MaxWidth;
			out << YAML::Key << "ScreenSpace" << YAML::Value << textComponent.ScreenSpace;
			out << YAML::Key << "DropShadow" << YAML::Value << textComponent.DropShadow;
			out << YAML::Key << "ShadowDistance" << YAML::Value << textComponent.ShadowDistance;
			out << YAML::Key << "ShadowColor" << YAML::Value << textComponent.ShadowColor;

			out << YAML::EndMap; // TextComponent
		}

		if (entity.HasComponent<RigidBody2DComponent>())
		{
			out << YAML::Key << "RigidBody2DComponent";
			out << YAML::BeginMap; // RigidBody2DComponent

			const auto& rigidbody2DComponent = entity.GetComponent<RigidBody2DComponent>();
			out << YAML::Key << "BodyType" << YAML::Value << (int)rigidbody2DComponent.BodyType;
			out << YAML::Key << "FixedRotation" << YAML::Value << rigidbody2DComponent.FixedRotation;
			out << YAML::Key << "Mass" << YAML::Value << rigidbody2DComponent.Mass;
			out << YAML::Key << "LinearDrag" << YAML::Value << rigidbody2DComponent.LinearDrag;
			out << YAML::Key << "AngularDrag" << YAML::Value << rigidbody2DComponent.AngularDrag;
			out << YAML::Key << "GravityScale" << YAML::Value << rigidbody2DComponent.GravityScale;
			out << YAML::Key << "IsBullet" << YAML::Value << rigidbody2DComponent.IsBullet;

			out << YAML::EndMap; // RigidBody2DComponent
		}

		if (entity.HasComponent<BoxCollider2DComponent>())
		{
			out << YAML::Key << "BoxCollider2DComponent";
			out << YAML::BeginMap; // BoxCollider2DComponent

			auto& boxCollider2DComponent = entity.GetComponent<BoxCollider2DComponent>();
			out << YAML::Key << "Offset" << YAML::Value << boxCollider2DComponent.Offset;
			out << YAML::Key << "Size" << YAML::Value << boxCollider2DComponent.Size;
			out << YAML::Key << "Density" << YAML::Value << boxCollider2DComponent.Density;
			out << YAML::Key << "Friction" << YAML::Value << boxCollider2DComponent.Friction;

			out << YAML::EndMap; // BoxCollider2DComponent
		}

		if (entity.HasComponent<CircleCollider2DComponent>())
		{
			out << YAML::Key << "CircleCollider2DComponent";
			out << YAML::BeginMap; // CircleCollider2DComponent

			auto& circleCollider2DComponent = entity.GetComponent<CircleCollider2DComponent>();
			out << YAML::Key << "Offset" << YAML::Value << circleCollider2DComponent.Offset;
			out << YAML::Key << "Radius" << YAML::Value << circleCollider2DComponent.Radius;
			out << YAML::Key << "Density" << YAML::Value << circleCollider2DComponent.Density;
			out << YAML::Key << "Friction" << YAML::Value << circleCollider2DComponent.Friction;

			out << YAML::EndMap; // CircleCollider2DComponent
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
		SerializeToYAML(out);

		// if extension is .auto, then only save if the scene has actually changed (determined by hashing serialized string)
		if (auto hash = std::hash<std::string>()(out.c_str()); (filepath.extension() != ".auto") || (hash != m_Scene->m_LastSerializeHash)) {
			std::ofstream fout(filepath);
			fout << out.c_str();
			m_Scene->m_LastSerializeHash = hash;
		}
	}

	void SceneSerializer::SerializeToYAML(YAML::Emitter& out)
	{
		out << YAML::BeginMap;
		out << YAML::Key << "Scene";
		out << YAML::Value << m_Scene->GetName();

		out << YAML::Key << "Entities";
		out << YAML::Value << YAML::BeginSeq;

		// Sort entities by UUID (for better serializing)
		std::map<UUID, entt::entity> sortedEntityMap;
		auto idComponentView = m_Scene->m_Registry.view<IDComponent>();
		for (auto entity : idComponentView)
			sortedEntityMap[idComponentView.get<IDComponent>(entity).ID] = entity;

		// Serialize sorted entities
		for (auto [id, entity] : sortedEntityMap)
			SerializeEntity(out, { entity, m_Scene.Raw() }, m_Scene);

		out << YAML::EndSeq;

		// Scene Audio
		//MiniAudioEngine::Get().SerializeSceneAudio(out, m_Scene);

		out << YAML::EndMap;
	}

	bool SceneSerializer::DeserializeFromYAML(const std::string& yamlString)
	{
		YAML::Node data = YAML::Load(yamlString);
		if (!data["Scene"])
			return false;

		std::string sceneName = data["Scene"].as<std::string>();
		SE_CORE_INFO_TAG("AssetManager", "Deserializing scene '{0}'", sceneName);
		m_Scene->SetName(sceneName);

		auto entities = data["Entities"];
		if (entities)
			DeserializeEntities(entities, m_Scene);

		/*
		auto sceneAudio = data["SceneAudio"];
		if (sceneAudio)
			MiniAudioEngine::Get().DeserializeSceneAudio(sceneAudio);
		*/

		// Sort IdComponent by by entity handle (which is essentially the order in which they were created)
		// This ensures a consistent ordering when iterating IdComponent (for example: when rendering scene hierarchy panel)
		m_Scene->m_Registry.sort<IDComponent>([this](const auto lhs, const auto rhs)
			{
				auto lhsEntity = m_Scene->m_EntityIDMap.find(lhs.ID);
				auto rhsEntity = m_Scene->m_EntityIDMap.find(rhs.ID);
				return static_cast<uint32_t>(lhsEntity->second) < static_cast<uint32_t>(rhsEntity->second);
			});

		return true;
	}

	void SceneSerializer::SerializeRuntime(AssetHandle scene)
	{
		// Not implemented
		SE_CORE_ASSERT(false);
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		std::ifstream stream(filepath);
		SE_CORE_ASSERT(stream);
		std::stringstream strStream;
		strStream << stream.rdbuf();

		try
		{
			DeserializeFromYAML(strStream.str());
			m_Scene->m_LastSerializeHash = std::hash<std::string>{}(strStream.str());
		}
		catch (const YAML::Exception& e)
		{
			SE_CONSOLE_LOG_ERROR("Failed to deserialize scene '{0}': {1}", filepath.string(), e.what());
			return false;
		}

		// Asset handle
		m_Scene->Handle = Project::GetEditorAssetManager()->GetAssetHandleFromFilePath(filepath);

		// NOTE(Peter): Fix for "UntitledScene" name, hardcoded which isn't good
		if (m_Scene->GetName() == "UntitledScene")
			m_Scene->SetName(Utils::RemoveExtension(filepath.filename().string()));

		return true;
	}

	bool SceneSerializer::DeserializeRuntime(const std::filesystem::path& filepath)
	{
		// Not implemented
		SE_CORE_ASSERT(false);
		return false;
	}

	void SceneSerializer::DeserializeEntities(YAML::Node& entitiesNode, Ref<Scene> scene)
	{
		auto checkAndAssignAssetHandle = [](AssetHandle& destination, AssetType expectedType, AssetHandle source) {
			if (AssetManager::IsAssetHandleValid(source))
			{
				AssetType type = AssetManager::GetAssetType(source);
				if (type == expectedType)
				{
					destination = source;
				}
				else
				{
					destination = 0;
					SE_CORE_ERROR_TAG("AssetManager", "Asset {} is not of expected type {}", source, Utils::AssetTypeToString(expectedType));
				}
			}
			else
			{
				destination = 0;
				SE_CORE_ERROR_TAG("AssetManager", "Missing asset {}", source);
			}
			};

		for (auto entity : entitiesNode)
		{
			uint64_t uuid = entity["Entity"].as<uint64_t>();

			std::string name;
			auto tagComponent = entity["TagComponent"];
			if (tagComponent)
				name = tagComponent["Tag"].as<std::string>();

			//SE_CORE_INFO("Deserialized Entity '{0}' with ID '{1}'", name, uuid);

			Entity deserializedEntity = scene->CreateEntityWithID(uuid, name, false);

			auto& relationshipComponent = deserializedEntity.GetComponent<RelationshipComponent>();
			uint64_t parentHandle = entity["Parent"] ? entity["Parent"].as<uint64_t>() : 0;
			relationshipComponent.ParentHandle = parentHandle;

			auto children = entity["Children"];
			if (children)
			{
				for (auto child : children)
				{
					uint64_t childHandle = child["Handle"].as<uint64_t>();
					relationshipComponent.Children.push_back(childHandle);
				}
			}

			auto transformComponent = entity["TransformComponent"];
			if (transformComponent)
			{
				// Entities always have transforms
				auto& transform = deserializedEntity.GetComponent<TransformComponent>();
				transform.Translation = transformComponent["Position"].as<glm::vec3>(glm::vec3(0.0f));
				auto rotationNode = transformComponent["Rotation"];
				// Some versions of Hazel serialized rotations as quaternions
				// They should be serialized as Euler angles (this is the only way to support rotations > 360 degrees)
				// If you encounter this VERIFY, then you can uncomment this code, load your scene in and then save it
				// That will convert rotations back to Euler angles, and you can then re-comment out this code.
				//SE_CORE_VERIFY(rotationNode.size() == 3, "Transform component rotation should be serialized as Euler angles. Found Quaternions!");
				if (rotationNode.size() == 4)
				{
					transform.SetRotation(transformComponent["Rotation"].as<glm::quat>(glm::quat()));
				}
				else
				{
					transform.SetRotationEuler(transformComponent["Rotation"].as<glm::vec3>(glm::vec3(0.0f)));
				}
				transform.Scale = transformComponent["Scale"].as<glm::vec3>();
			}

			auto scriptComponent = entity["ScriptComponent"];
			if (scriptComponent)
			{
				try
				{
					uint64_t scriptID = scriptComponent["ScriptID"].as<uint64_t>(0);

					if (scriptID == 0)
					{
						scriptID = scriptComponent["ClassHandle"].as<uint64_t>(0);
					}

					if (scriptID != 0)
					{
						auto& scriptEngine = ScriptEngine::GetMutable();

						if (scriptEngine.IsValidScript(scriptID))
						{
							const auto& scriptMetadata = scriptEngine.GetScriptMetadata(scriptID);

							ScriptComponent& sc = deserializedEntity.AddComponent<ScriptComponent>();
							sc.ScriptID = scriptID;

							scene->m_ScriptStorage.InitializeEntityStorage(scriptID, deserializedEntity.GetUUID());

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

								if (oldFormat)
								{
									// Old format, try generating id from name
									auto fullFieldName = std::format("{}.{}", scriptMetadata.FullName, fieldName);
									fieldID = Hash::GenerateFNVHash(fullFieldName);
								}

								if (scriptMetadata.Fields.contains(fieldID))
								{
									const auto& fieldMetadata = scriptMetadata.Fields.at(fieldID);
									auto& fieldStorage = scene->m_ScriptStorage.EntityStorage.at(deserializedEntity.GetUUID()).Fields[fieldID];

									auto valueNode = oldFormat ? field["Data"] : field["Value"];

									if (fieldStorage.IsArray())
									{
										SE_CORE_VERIFY(valueNode.IsSequence());
										fieldStorage.Resize(valueNode.size());

										for (int32_t i = 0; i < valueNode.size(); i++)
										{
											switch (fieldMetadata.Type)
											{
											case DataType::Bool:
											{
												fieldStorage.SetValue(valueNode[i].as<bool>());
												break;
											}
											case DataType::SByte:
											{
												fieldStorage.SetValue(valueNode[i].as<int8_t>(), i);
												break;
											}
											case DataType::Byte:
											{
												fieldStorage.SetValue(valueNode[i].as<uint8_t>(), i);
												break;
											}
											case DataType::Short:
											{
												fieldStorage.SetValue(valueNode[i].as<int16_t>(), i);
												break;
											}
											case DataType::UShort:
											{
												fieldStorage.SetValue(valueNode[i].as<uint16_t>(), i);
												break;
											}
											case DataType::Int:
											{
												fieldStorage.SetValue(valueNode[i].as<int32_t>(), i);
												break;
											}
											case DataType::UInt:
											{
												fieldStorage.SetValue(valueNode[i].as<uint32_t>(), i);
												break;
											}
											case DataType::Long:
											{
												fieldStorage.SetValue(valueNode[i].as<int64_t>(), i);
												break;
											}
											case DataType::ULong:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											case DataType::Float:
											{
												fieldStorage.SetValue(valueNode[i].as<float>(), i);
												break;
											}
											case DataType::Double:
											{
												fieldStorage.SetValue(valueNode[i].as<double>(), i);
												break;
											}
											case DataType::Vector2:
											{
												fieldStorage.SetValue(valueNode[i].as<glm::vec2>(), i);
												break;
											}
											case DataType::Vector3:
											{
												fieldStorage.SetValue(valueNode[i].as<glm::vec3>(), i);
												break;
											}
											case DataType::Vector4:
											{
												fieldStorage.SetValue(valueNode[i].as<glm::vec4>(), i);
												break;
											}
											case DataType::String:
											{
												fieldStorage.SetValue(valueNode[i].as<std::string>(), i);
												break;
											}
											case DataType::Entity:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											case DataType::Prefab:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											case DataType::Mesh:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											case DataType::StaticMesh:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											case DataType::Material:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											case DataType::Texture2D:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											case DataType::Scene:
											{
												fieldStorage.SetValue(valueNode[i].as<uint64_t>(), i);
												break;
											}
											default:
												break;
											}
										}
									}
									else
									{
										switch (fieldMetadata.Type)
										{
										case DataType::Bool:
										{
											fieldStorage.SetValue(valueNode.as<bool>());
											break;
										}
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
										case DataType::String:
										{
											fieldStorage.SetValue(valueNode.as<std::string>());
											break;
										}
										case DataType::Entity:
										{
											fieldStorage.SetValue(valueNode.as<uint64_t>());
											break;
										}
										case DataType::Prefab:
										{
											fieldStorage.SetValue(valueNode.as<uint64_t>());
											break;
										}
										case DataType::Mesh:
										{
											fieldStorage.SetValue(valueNode.as<uint64_t>());
											break;
										}
										case DataType::StaticMesh:
										{
											fieldStorage.SetValue(valueNode.as<uint64_t>());
											break;
										}
										case DataType::Material:
										{
											fieldStorage.SetValue(valueNode.as<uint64_t>());
											break;
										}
										case DataType::Texture2D:
										{
											fieldStorage.SetValue(valueNode.as<uint64_t>());
											break;
										}
										case DataType::Scene:
										{
											fieldStorage.SetValue(valueNode.as<uint64_t>());
											break;
										}
										default:
											break;
										}
									}
								}
							}
						}
					}
					else
					{
						SE_CORE_ERROR_TAG("Scripting", "Failed to deserialize ScriptComponent on entity '{}', script id of 0 is not valid.", uuid);
					}
				}
				catch (const std::exception& e)
				{
					SE_CORE_ERROR_TAG("ScriptEngine", "Failed to deserialize ScriptComponent on entity '{}' : {}", uuid, e.what());
				}
			}

			auto cameraComponent = entity["CameraComponent"];
			if (cameraComponent)
			{
				auto& component = deserializedEntity.AddComponent<CameraComponent>();
				const auto& cameraNode = cameraComponent["Camera"];

				component.Camera = SceneCamera();
				auto& camera = component.Camera;

				if (cameraNode.IsMap())
				{
					if (cameraNode["ProjectionType"])
						camera.SetProjectionType((SceneCamera::ProjectionType)cameraNode["ProjectionType"].as<int>());
					if (cameraNode["PerspectiveFOV"])
						camera.SetDegPerspectiveVerticalFOV(cameraNode["PerspectiveFOV"].as<float>());
					if (cameraNode["PerspectiveNear"])
						camera.SetPerspectiveNearClip(cameraNode["PerspectiveNear"].as<float>());
					if (cameraNode["PerspectiveFar"])
						camera.SetPerspectiveFarClip(cameraNode["PerspectiveFar"].as<float>());
					if (cameraNode["OrthographicSize"])
						camera.SetOrthographicSize(cameraNode["OrthographicSize"].as<float>());
					if (cameraNode["OrthographicNear"])
						camera.SetOrthographicNearClip(cameraNode["OrthographicNear"].as<float>());
					if (cameraNode["OrthographicFar"])
						camera.SetOrthographicFarClip(cameraNode["OrthographicFar"].as<float>());
				}

				component.Primary = cameraComponent["Primary"].as<bool>();
			}

			auto spriteRendererComponent = entity["SpriteRendererComponent"];
			if (spriteRendererComponent)
			{
				auto& component = deserializedEntity.AddComponent<SpriteRendererComponent>();
				component.Color = spriteRendererComponent["Color"].as<glm::vec4>();
				checkAndAssignAssetHandle(component.Texture, AssetType::Texture, spriteRendererComponent["Texture"].as<AssetHandle>(0));
				component.TilingFactor = spriteRendererComponent["TilingFactor"].as<float>();
				if (spriteRendererComponent["UVStart"])
					component.UVStart = spriteRendererComponent["UVStart"].as<glm::vec2>();
				if (spriteRendererComponent["UVEnd"])
					component.UVEnd = spriteRendererComponent["UVEnd"].as<glm::vec2>();
				if (spriteRendererComponent["ScreenSpace"])
					component.ScreenSpace = spriteRendererComponent["ScreenSpace"].as<bool>();
			}

			auto textComponent = entity["TextComponent"];
			if (textComponent)
			{
				auto& component = deserializedEntity.AddComponent<TextComponent>();
				component.TextString = textComponent["TextString"].as<std::string>();
				component.TextHash = std::hash<std::string>()(component.TextString);
				checkAndAssignAssetHandle(component.FontHandle, AssetType::Font, textComponent["FontHandle"].as<AssetHandle>(0));
				if (!component.FontHandle)
					component.FontHandle = Font::GetDefaultFont()->Handle;

				component.Color = textComponent["Color"].as<glm::vec4>();
				component.LineSpacing = textComponent["LineSpacing"].as<float>();
				component.Kerning = textComponent["Kerning"].as<float>();
				component.MaxWidth = textComponent["MaxWidth"].as<float>();
				if (textComponent["ScreenSpace"])
					component.ScreenSpace = textComponent["ScreenSpace"].as<bool>();
				if (textComponent["DropShadow"])
					component.DropShadow = textComponent["DropShadow"].as<bool>();
				if (textComponent["ShadowDistance"])
					component.ShadowDistance = textComponent["ShadowDistance"].as<float>();
				if (textComponent["ShadowColor"])
					component.ShadowColor = textComponent["ShadowColor"].as<glm::vec4>();
			}

			auto rigidBody2DComponent = entity["RigidBody2DComponent"];
			if (rigidBody2DComponent)
			{
				auto& component = deserializedEntity.AddComponent<RigidBody2DComponent>();
				component.BodyType = (RigidBody2DComponent::Type)rigidBody2DComponent["BodyType"].as<int>();
				component.FixedRotation = rigidBody2DComponent["FixedRotation"] ? rigidBody2DComponent["FixedRotation"].as<bool>() : false;
				component.Mass = rigidBody2DComponent["Mass"].as<float>(1.0f);
				component.LinearDrag = rigidBody2DComponent["LinearDrag"].as<float>(0.01f);
				component.AngularDrag = rigidBody2DComponent["AngularDrag"].as<float>(0.05f);
				component.GravityScale = rigidBody2DComponent["GravityScale"].as<float>(1.0f);
				component.IsBullet = rigidBody2DComponent["IsBullet"].as<bool>(false);
			}

			auto boxCollider2DComponent = entity["BoxCollider2DComponent"];
			if (boxCollider2DComponent)
			{
				auto& component = deserializedEntity.AddComponent<BoxCollider2DComponent>();
				component.Offset = boxCollider2DComponent["Offset"].as<glm::vec2>();
				component.Size = boxCollider2DComponent["Size"].as<glm::vec2>();
				component.Density = boxCollider2DComponent["Density"] ? boxCollider2DComponent["Density"].as<float>() : 1.0f;
				component.Friction = boxCollider2DComponent["Friction"] ? boxCollider2DComponent["Friction"].as<float>() : 1.0f;
			}

			auto circleCollider2DComponent = entity["CircleCollider2DComponent"];
			if (circleCollider2DComponent)
			{
				auto& component = deserializedEntity.AddComponent<CircleCollider2DComponent>();
				component.Offset = circleCollider2DComponent["Offset"].as<glm::vec2>();
				component.Radius = circleCollider2DComponent["Radius"].as<float>();
				component.Density = circleCollider2DComponent["Density"] ? circleCollider2DComponent["Density"].as<float>() : 1.0f;
				component.Friction = circleCollider2DComponent["Friction"] ? circleCollider2DComponent["Friction"].as<float>() : 1.0f;
			}
		}
	}

}
