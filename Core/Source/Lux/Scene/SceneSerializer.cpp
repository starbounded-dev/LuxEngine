#include "lpch.h"
#include "SceneSerializer.h"

#include "Components.h"
#include "Entity.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Scripting/ScriptEngine.h"

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

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
	struct convert<Lux::UUID>
	{
		static Node encode(const Lux::UUID& uuid)
		{
			Node node;
			node.push_back((uint64_t)uuid);
			return node;
		}

		static bool decode(const Node& node, Lux::UUID& uuid)
		{
			uuid = node.as<uint64_t>();
			return true;
		}
	};

}

namespace Lux {

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v)
	{
		out << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
		return out;
	}

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v)
	{
		out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
		return out;
	}

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
	{
		out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
		return out;
	}

	namespace {

		static void SerializeMaterialTable(YAML::Emitter& out, const Ref<MaterialTable>& materialTable)
		{
			out << YAML::Key << "MaterialTable" << YAML::Value << YAML::BeginMap;
			if (materialTable)
			{
				for (const auto& [index, handle] : materialTable->GetMaterials())
					out << YAML::Key << index << YAML::Value << handle;
			}
			out << YAML::EndMap;
		}

		static Ref<MaterialTable> DeserializeMaterialTable(const YAML::Node& materialTableNode)
		{
			Ref<MaterialTable> materialTable = Ref<MaterialTable>::Create();
			if (!materialTableNode || !materialTableNode.IsMap())
				return materialTable;

			uint32_t materialCount = 0;
			for (auto material : materialTableNode)
			{
				const uint32_t index = material.first.as<uint32_t>();
				const AssetHandle handle = material.second.as<AssetHandle>();
				materialTable->SetMaterial(index, handle);
				materialCount = std::max(materialCount, index + 1);
			}
			materialTable->SetMaterialCount(std::max(materialCount, materialTable->GetMaterialCount()));
			return materialTable;
		}

		static bool ContainsLegacyOrDeferredSceneData(const YAML::Node& entity)
		{
			if (entity["RelationshipComponent"])
				return true;

			if (auto transform = entity["TransformComponent"])
			{
				if (transform["Translation"])
					return true;
			}

			if (auto prefab = entity["PrefabComponent"])
			{
				if (prefab["PrefabID"] || prefab["EntityID"])
					return true;
			}

			if (auto meshTag = entity["MeshTagComponent"])
			{
				if (meshTag["MeshName"])
					return true;
			}

			if (auto staticMesh = entity["StaticMeshComponent"])
			{
				if (staticMesh["Mesh"] || staticMesh["CastShadows"])
					return true;
			}

			return entity["AudioData"] || entity["AudioSourceComponent"] || entity["AudioListenerComponent"]
				|| entity["AnimationComponent"] || entity["RigidBodyComponent"] || entity["CharacterControllerComponent"]
				|| entity["CompoundColliderComponent"] || entity["BoxColliderComponent"] || entity["SphereColliderComponent"]
				|| entity["CapsuleColliderComponent"] || entity["MeshColliderComponent"];
		}

		static void SerializeEntity(YAML::Emitter& out, Entity entity)
		{
			LUX_CORE_ASSERT(entity.HasComponent<IDComponent>());

			out << YAML::BeginMap;
			out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

			if (entity.HasComponent<TagComponent>())
			{
				out << YAML::Key << "TagComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Tag" << YAML::Value << entity.GetComponent<TagComponent>().Tag;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<RelationshipComponent>())
			{
				const auto& relationship = entity.GetComponent<RelationshipComponent>();
				out << YAML::Key << "Parent" << YAML::Value << relationship.ParentHandle;
				out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
				for (UUID child : relationship.Children)
				{
					out << YAML::BeginMap;
					out << YAML::Key << "Handle" << YAML::Value << child;
					out << YAML::EndMap;
				}
				out << YAML::EndSeq;
			}

			if (entity.HasComponent<PrefabComponent>())
			{
				const auto& prefab = entity.GetComponent<PrefabComponent>();
				out << YAML::Key << "PrefabComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Prefab" << YAML::Value << prefab.PrefabID;
				out << YAML::Key << "Entity" << YAML::Value << prefab.EntityID;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<TransformComponent>())
			{
				const auto& transform = entity.GetComponent<TransformComponent>();
				out << YAML::Key << "TransformComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Position" << YAML::Value << transform.Translation;
				out << YAML::Key << "Rotation" << YAML::Value << transform.GetRotationEuler();
				out << YAML::Key << "Scale" << YAML::Value << transform.Scale;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<ScriptComponent>())
			{
				const auto& script = entity.GetComponent<ScriptComponent>();
				out << YAML::Key << "ScriptComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "ClassName" << YAML::Value << script.ClassName;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<MeshComponent>())
			{
				out << YAML::Key << "MeshComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "AssetID" << YAML::Value << entity.GetComponent<MeshComponent>().Mesh;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<MeshTagComponent>())
			{
				out << YAML::Key << "MeshTagComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "EntityID" << YAML::Value << entity.GetComponent<MeshTagComponent>().MeshEntity;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<SubmeshComponent>())
			{
				const auto& submesh = entity.GetComponent<SubmeshComponent>();
				out << YAML::Key << "SubmeshComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "AssetID" << YAML::Value << submesh.Mesh;
				out << YAML::Key << "SubmeshIndex" << YAML::Value << submesh.SubmeshIndex;
				SerializeMaterialTable(out, submesh.MaterialTable);
				out << YAML::Key << "Visible" << YAML::Value << submesh.Visible;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<StaticMeshComponent>())
			{
				const auto& staticMesh = entity.GetComponent<StaticMeshComponent>();
				out << YAML::Key << "StaticMeshComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "AssetID" << YAML::Value << staticMesh.StaticMesh;
				SerializeMaterialTable(out, staticMesh.MaterialTable);
				out << YAML::Key << "Visible" << YAML::Value << staticMesh.Visible;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CameraComponent>())
			{
				const auto& cameraComponent = entity.GetComponent<CameraComponent>();
				const SceneCamera& camera = cameraComponent.Camera;
				out << YAML::Key << "CameraComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Camera" << YAML::Value;
				out << YAML::BeginMap;
				out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
				out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetDegPerspectiveVerticalFOV();
				out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
				out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
				out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
				out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
				out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
				out << YAML::EndMap;
				out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
				out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<DirectionalLightComponent>())
			{
				const auto& light = entity.GetComponent<DirectionalLightComponent>();
				out << YAML::Key << "DirectionalLightComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
				out << YAML::Key << "Radiance" << YAML::Value << light.Radiance;
				out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;
				out << YAML::Key << "SoftShadows" << YAML::Value << light.SoftShadows;
				out << YAML::Key << "LightSize" << YAML::Value << light.LightSize;
				out << YAML::Key << "ShadowAmount" << YAML::Value << light.ShadowAmount;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<PointLightComponent>())
			{
				const auto& light = entity.GetComponent<PointLightComponent>();
				out << YAML::Key << "PointLightComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Radiance" << YAML::Value << light.Radiance;
				out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
				out << YAML::Key << "CastShadows" << YAML::Value << light.CastsShadows;
				out << YAML::Key << "SoftShadows" << YAML::Value << light.SoftShadows;
				out << YAML::Key << "MinRadius" << YAML::Value << light.MinRadius;
				out << YAML::Key << "Radius" << YAML::Value << light.Radius;
				out << YAML::Key << "LightSize" << YAML::Value << light.LightSize;
				out << YAML::Key << "Falloff" << YAML::Value << light.Falloff;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<SpotLightComponent>())
			{
				const auto& light = entity.GetComponent<SpotLightComponent>();
				out << YAML::Key << "SpotLightComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Radiance" << YAML::Value << light.Radiance;
				out << YAML::Key << "Angle" << YAML::Value << light.Angle;
				out << YAML::Key << "AngleAttenuation" << YAML::Value << light.AngleAttenuation;
				out << YAML::Key << "CastsShadows" << YAML::Value << light.CastsShadows;
				out << YAML::Key << "SoftShadows" << YAML::Value << light.SoftShadows;
				out << YAML::Key << "Falloff" << YAML::Value << light.Falloff;
				out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
				out << YAML::Key << "Range" << YAML::Value << light.Range;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<SkyLightComponent>())
			{
				const auto& skyLight = entity.GetComponent<SkyLightComponent>();
				out << YAML::Key << "SkyLightComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "EnvironmentMap" << YAML::Value << (AssetManager::GetMemoryAsset(skyLight.SceneEnvironment) ? (AssetHandle)0 : skyLight.SceneEnvironment);
				out << YAML::Key << "Intensity" << YAML::Value << skyLight.Intensity;
				out << YAML::Key << "Lod" << YAML::Value << skyLight.Lod;
				out << YAML::Key << "DynamicSky" << YAML::Value << skyLight.DynamicSky;
				if (skyLight.DynamicSky)
					out << YAML::Key << "TurbidityAzimuthInclination" << YAML::Value << skyLight.TurbidityAzimuthInclination;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<SpriteRendererComponent>())
			{
				const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
				out << YAML::Key << "SpriteRendererComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Color" << YAML::Value << sprite.Color;
				out << YAML::Key << "Texture" << YAML::Value << sprite.Texture;
				out << YAML::Key << "TilingFactor" << YAML::Value << sprite.TilingFactor;
				out << YAML::Key << "UVStart" << YAML::Value << sprite.UVStart;
				out << YAML::Key << "UVEnd" << YAML::Value << sprite.UVEnd;
				out << YAML::Key << "ScreenSpace" << YAML::Value << sprite.ScreenSpace;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CircleRendererComponent>())
			{
				const auto& circle = entity.GetComponent<CircleRendererComponent>();
				out << YAML::Key << "CircleRendererComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Color" << YAML::Value << circle.Color;
				out << YAML::Key << "Thickness" << YAML::Value << circle.Thickness;
				out << YAML::Key << "Fade" << YAML::Value << circle.Fade;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<TextComponent>())
			{
				const auto& text = entity.GetComponent<TextComponent>();
				out << YAML::Key << "TextComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "TextString" << YAML::Value << text.TextString;
				out << YAML::Key << "FontHandle" << YAML::Value << text.FontHandle;
				out << YAML::Key << "Color" << YAML::Value << text.Color;
				out << YAML::Key << "LineSpacing" << YAML::Value << text.LineSpacing;
				out << YAML::Key << "Kerning" << YAML::Value << text.Kerning;
				out << YAML::Key << "MaxWidth" << YAML::Value << text.MaxWidth;
				out << YAML::Key << "ScreenSpace" << YAML::Value << text.ScreenSpace;
				out << YAML::Key << "DropShadow" << YAML::Value << text.DropShadow;
				out << YAML::Key << "ShadowDistance" << YAML::Value << text.ShadowDistance;
				out << YAML::Key << "ShadowColor" << YAML::Value << text.ShadowColor;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<RigidBody2DComponent>())
			{
				const auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
				out << YAML::Key << "RigidBody2DComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "BodyType" << YAML::Value << (int)rb2d.BodyType;
				out << YAML::Key << "FixedRotation" << YAML::Value << rb2d.FixedRotation;
				out << YAML::Key << "Mass" << YAML::Value << rb2d.Mass;
				out << YAML::Key << "LinearDrag" << YAML::Value << rb2d.LinearDrag;
				out << YAML::Key << "AngularDrag" << YAML::Value << rb2d.AngularDrag;
				out << YAML::Key << "GravityScale" << YAML::Value << rb2d.GravityScale;
				out << YAML::Key << "IsBullet" << YAML::Value << rb2d.IsBullet;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<BoxCollider2DComponent>())
			{
				const auto& collider = entity.GetComponent<BoxCollider2DComponent>();
				out << YAML::Key << "BoxCollider2DComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Size" << YAML::Value << collider.Size;
				out << YAML::Key << "Density" << YAML::Value << collider.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Friction;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CircleCollider2DComponent>())
			{
				const auto& collider = entity.GetComponent<CircleCollider2DComponent>();
				out << YAML::Key << "CircleCollider2DComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Radius" << YAML::Value << collider.Radius;
				out << YAML::Key << "Density" << YAML::Value << collider.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Friction;
				out << YAML::EndMap;
			}

			out << YAML::EndMap;
		}

		static bool DeserializeEntities(const YAML::Node& entities, Ref<Scene> scene)
		{
			if (!entities)
				return true;

			for (auto entity : entities)
			{
				if (ContainsLegacyOrDeferredSceneData(entity))
				{
					LUX_CORE_ERROR("Scene contains legacy Lux or deferred scene component data; refusing to deserialize Hazel-only scene schema.");
					return false;
				}

				const UUID uuid = entity["Entity"].as<uint64_t>();
				std::string name = "Entity";
				if (auto tag = entity["TagComponent"])
					name = tag["Tag"].as<std::string>("Entity");

				scene->CreateEntityWithID(uuid, name, false);
			}

			for (auto entity : entities)
			{
				Entity deserializedEntity = scene->GetEntityWithUUID(entity["Entity"].as<uint64_t>());

				if (auto parent = entity["Parent"])
					deserializedEntity.GetComponent<RelationshipComponent>().ParentHandle = parent.as<uint64_t>();

				if (auto children = entity["Children"])
				{
					auto& childList = deserializedEntity.GetComponent<RelationshipComponent>().Children;
					childList.clear();
					for (auto child : children)
					{
						if (auto handle = child["Handle"])
							childList.emplace_back(handle.as<uint64_t>());
					}
				}

				if (auto prefab = entity["PrefabComponent"])
				{
					auto& component = deserializedEntity.AddComponent<PrefabComponent>();
					component.PrefabID = prefab["Prefab"].as<uint64_t>(0);
					component.EntityID = prefab["Entity"].as<uint64_t>(0);
				}

				if (auto transform = entity["TransformComponent"])
				{
					auto& component = deserializedEntity.GetComponent<TransformComponent>();
					if (!transform["Position"] || !transform["Rotation"] || !transform["Scale"])
						return false;

					component.Translation = transform["Position"].as<glm::vec3>();
					component.SetRotationEuler(transform["Rotation"].as<glm::vec3>());
					component.Scale = transform["Scale"].as<glm::vec3>();
				}

				if (auto script = entity["ScriptComponent"])
				{
					auto& component = deserializedEntity.AddComponent<ScriptComponent>();
					component.ClassName = script["ClassName"].as<std::string>("");
				}

				if (auto mesh = entity["MeshComponent"])
				{
					auto& component = deserializedEntity.AddComponent<MeshComponent>();
					component.Mesh = mesh["AssetID"].as<uint64_t>(0);
				}

				if (auto meshTag = entity["MeshTagComponent"])
				{
					auto& component = deserializedEntity.AddComponent<MeshTagComponent>();
					component.MeshEntity = meshTag["EntityID"].as<uint64_t>(0);
				}

				if (auto submesh = entity["SubmeshComponent"])
				{
					auto& component = deserializedEntity.AddComponent<SubmeshComponent>();
					component.Mesh = submesh["AssetID"].as<uint64_t>(0);
					component.SubmeshIndex = submesh["SubmeshIndex"].as<uint32_t>(0);
					component.MaterialTable = DeserializeMaterialTable(submesh["MaterialTable"]);
					component.Visible = submesh["Visible"].as<bool>(true);
				}

				if (auto staticMesh = entity["StaticMeshComponent"])
				{
					auto& component = deserializedEntity.AddComponent<StaticMeshComponent>();
					component.StaticMesh = staticMesh["AssetID"].as<uint64_t>(0);
					component.MaterialTable = DeserializeMaterialTable(staticMesh["MaterialTable"]);
					component.Visible = staticMesh["Visible"].as<bool>(true);
				}

				if (auto camera = entity["CameraComponent"])
				{
					auto& component = deserializedEntity.AddComponent<CameraComponent>();
					auto cameraProps = camera["Camera"];
					component.Camera.SetProjectionType((SceneCamera::ProjectionType)cameraProps["ProjectionType"].as<int>(0));
					component.Camera.SetDegPerspectiveVerticalFOV(cameraProps["PerspectiveFOV"].as<float>(45.0f));
					component.Camera.SetPerspectiveNearClip(cameraProps["PerspectiveNear"].as<float>(0.1f));
					component.Camera.SetPerspectiveFarClip(cameraProps["PerspectiveFar"].as<float>(1000.0f));
					component.Camera.SetOrthographicSize(cameraProps["OrthographicSize"].as<float>(10.0f));
					component.Camera.SetOrthographicNearClip(cameraProps["OrthographicNear"].as<float>(-1.0f));
					component.Camera.SetOrthographicFarClip(cameraProps["OrthographicFar"].as<float>(1.0f));
					component.Primary = camera["Primary"].as<bool>(true);
					component.FixedAspectRatio = camera["FixedAspectRatio"].as<bool>(false);
				}

				if (auto light = entity["DirectionalLightComponent"])
				{
					auto& component = deserializedEntity.AddComponent<DirectionalLightComponent>();
					component.Intensity = light["Intensity"].as<float>(1.0f);
					component.Radiance = light["Radiance"].as<glm::vec3>(glm::vec3(1.0f));
					component.CastShadows = light["CastShadows"].as<bool>(true);
					component.SoftShadows = light["SoftShadows"].as<bool>(true);
					component.LightSize = light["LightSize"].as<float>(0.5f);
					component.ShadowAmount = light["ShadowAmount"].as<float>(1.0f);
				}

				if (auto light = entity["PointLightComponent"])
				{
					auto& component = deserializedEntity.AddComponent<PointLightComponent>();
					component.Radiance = light["Radiance"].as<glm::vec3>(glm::vec3(1.0f));
					component.Intensity = light["Intensity"].as<float>(1.0f);
					component.CastsShadows = light["CastShadows"].as<bool>(true);
					component.SoftShadows = light["SoftShadows"].as<bool>(true);
					component.MinRadius = light["MinRadius"].as<float>(1.0f);
					component.Radius = light["Radius"].as<float>(10.0f);
					component.LightSize = light["LightSize"].as<float>(0.5f);
					component.Falloff = light["Falloff"].as<float>(1.0f);
				}

				if (auto light = entity["SpotLightComponent"])
				{
					auto& component = deserializedEntity.AddComponent<SpotLightComponent>();
					component.Radiance = light["Radiance"].as<glm::vec3>(glm::vec3(1.0f));
					component.Intensity = light["Intensity"].as<float>(1.0f);
					component.Range = light["Range"].as<float>(10.0f);
					component.Angle = light["Angle"].as<float>(60.0f);
					component.AngleAttenuation = light["AngleAttenuation"].as<float>(5.0f);
					component.CastsShadows = light["CastsShadows"].as<bool>(false);
					component.SoftShadows = light["SoftShadows"].as<bool>(false);
					component.Falloff = light["Falloff"].as<float>(1.0f);
				}

				if (auto skyLight = entity["SkyLightComponent"])
				{
					auto& component = deserializedEntity.AddComponent<SkyLightComponent>();
					component.SceneEnvironment = skyLight["EnvironmentMap"].as<uint64_t>(0);
					component.Intensity = skyLight["Intensity"].as<float>(1.0f);
					component.Lod = skyLight["Lod"].as<float>(0.0f);
					component.DynamicSky = skyLight["DynamicSky"].as<bool>(false);
					component.TurbidityAzimuthInclination = skyLight["TurbidityAzimuthInclination"].as<glm::vec3>(glm::vec3{ 2.0f, 0.0f, 0.0f });
				}

				if (auto sprite = entity["SpriteRendererComponent"])
				{
					auto& component = deserializedEntity.AddComponent<SpriteRendererComponent>();
					component.Color = sprite["Color"].as<glm::vec4>(glm::vec4(1.0f));
					component.Texture = sprite["Texture"].as<uint64_t>(0);
					component.TilingFactor = sprite["TilingFactor"].as<float>(1.0f);
					component.UVStart = sprite["UVStart"].as<glm::vec2>(glm::vec2{ 0.0f, 0.0f });
					component.UVEnd = sprite["UVEnd"].as<glm::vec2>(glm::vec2{ 1.0f, 1.0f });
					component.ScreenSpace = sprite["ScreenSpace"].as<bool>(false);
				}

				if (auto circle = entity["CircleRendererComponent"])
				{
					auto& component = deserializedEntity.AddComponent<CircleRendererComponent>();
					component.Color = circle["Color"].as<glm::vec4>(glm::vec4(1.0f));
					component.Thickness = circle["Thickness"].as<float>(1.0f);
					component.Fade = circle["Fade"].as<float>(0.005f);
				}

				if (auto text = entity["TextComponent"])
				{
					auto& component = deserializedEntity.AddComponent<TextComponent>();
					component.TextString = text["TextString"].as<std::string>("");
					component.FontHandle = text["FontHandle"].as<uint64_t>(0);
					component.Color = text["Color"].as<glm::vec4>(glm::vec4(1.0f));
					component.LineSpacing = text["LineSpacing"].as<float>(0.0f);
					component.Kerning = text["Kerning"].as<float>(0.0f);
					component.MaxWidth = text["MaxWidth"].as<float>(10.0f);
					component.ScreenSpace = text["ScreenSpace"].as<bool>(false);
					component.DropShadow = text["DropShadow"].as<bool>(false);
					component.ShadowDistance = text["ShadowDistance"].as<float>(0.0f);
					component.ShadowColor = text["ShadowColor"].as<glm::vec4>(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
				}

				if (auto rigidBody = entity["RigidBody2DComponent"])
				{
					auto& component = deserializedEntity.AddComponent<RigidBody2DComponent>();
					component.BodyType = (RigidBody2DComponent::Type)rigidBody["BodyType"].as<int>((int)RigidBody2DComponent::Type::Static);
					component.FixedRotation = rigidBody["FixedRotation"].as<bool>(false);
					component.Mass = rigidBody["Mass"].as<float>(1.0f);
					component.LinearDrag = rigidBody["LinearDrag"].as<float>(0.01f);
					component.AngularDrag = rigidBody["AngularDrag"].as<float>(0.05f);
					component.GravityScale = rigidBody["GravityScale"].as<float>(1.0f);
					component.IsBullet = rigidBody["IsBullet"].as<bool>(false);
				}

				if (auto boxCollider = entity["BoxCollider2DComponent"])
				{
					auto& component = deserializedEntity.AddComponent<BoxCollider2DComponent>();
					component.Offset = boxCollider["Offset"].as<glm::vec2>(glm::vec2{ 0.0f, 0.0f });
					component.Size = boxCollider["Size"].as<glm::vec2>(glm::vec2{ 0.5f, 0.5f });
					component.Density = boxCollider["Density"].as<float>(1.0f);
					component.Friction = boxCollider["Friction"].as<float>(1.0f);
				}

				if (auto circleCollider = entity["CircleCollider2DComponent"])
				{
					auto& component = deserializedEntity.AddComponent<CircleCollider2DComponent>();
					component.Offset = circleCollider["Offset"].as<glm::vec2>(glm::vec2{ 0.0f, 0.0f });
					component.Radius = circleCollider["Radius"].as<float>(1.0f);
					component.Density = circleCollider["Density"].as<float>(1.0f);
					component.Friction = circleCollider["Friction"].as<float>(1.0f);
				}
			}

			scene->SortEntities();
			return true;
		}
	}

	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{
	}

	void SceneSerializer::SerializeToYAML(YAML::Emitter& out)
	{
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << m_Scene->GetName();
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

		auto view = m_Scene->m_Registry.view<IDComponent>();
		for (auto entityID : view)
			SerializeEntity(out, { entityID, m_Scene.get() });

		out << YAML::EndSeq;
		out << YAML::EndMap;
	}

	void SceneSerializer::Serialize(const std::filesystem::path& filepath)
	{
		YAML::Emitter out;
		SerializeToYAML(out);

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	void SceneSerializer::SerializeRuntime(const std::filesystem::path& filepath)
	{
		Serialize(filepath);
	}

	bool SceneSerializer::DeserializeFromYAML(const std::string& yamlString)
	{
		YAML::Node data = YAML::Load(yamlString);
		if (!data["Scene"])
			return false;

		m_Scene->m_Registry.clear();
		m_Scene->m_EntityMap.clear();
		m_Scene->SetName(data["Scene"].as<std::string>());

		return DeserializeEntities(data["Entities"], m_Scene);
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		std::ifstream stream(filepath);
		if (!stream.is_open())
			return false;

		std::stringstream strStream;
		strStream << stream.rdbuf();

		try
		{
			if (!DeserializeFromYAML(strStream.str()))
				return false;
		}
		catch (const YAML::Exception& e)
		{
			LUX_CORE_ERROR("Failed to deserialize scene '{0}': {1}", filepath.string(), e.what());
			return false;
		}

		if (Ref<Project> project = Project::GetActive())
		{
			if (Ref<EditorAssetManager> assetManager = Project::GetEditorAssetManager())
			{
				AssetHandle handle = assetManager->GetAssetHandleFromFilePath(filepath);
				if (handle)
					m_Scene->Handle = handle;
			}
		}

		if (m_Scene->GetName().empty() || m_Scene->GetName() == "Untitled" || m_Scene->GetName() == "UntitledScene")
			m_Scene->SetName(filepath.stem().string());

		return true;
	}

	bool SceneSerializer::DeserializeRuntime(const std::filesystem::path& filepath)
	{
		return Deserialize(filepath);
	}

	bool SceneSerializer::SerializeToAssetPack(FileStreamWriter& stream, AssetSerializationInfo& outInfo)
	{
		YAML::Emitter out;
		SerializeToYAML(out);

		outInfo.Offset = stream.GetStreamPosition();
		std::string yamlString = out.c_str();
		stream.WriteString(yamlString);
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	bool SceneSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::SceneInfo& sceneInfo)
	{
		stream.SetStreamPosition(sceneInfo.PackedOffset);
		std::string sceneYAML;
		stream.ReadString(sceneYAML);
		return DeserializeFromYAML(sceneYAML);
	}

}
