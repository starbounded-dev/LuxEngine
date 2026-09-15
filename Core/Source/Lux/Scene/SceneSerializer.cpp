#include "lpch.h"
#include "SceneSerializer.h"

#include "Components.h"
#include "Entity.h"
#include "Prefab.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Core/Hash.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

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

			// AudioSourceComponent/AudioListenerComponent used to be unimplemented (silently dropped
			// on load), so their mere presence was a legacy/incompatible-schema signal; now that
			// both round-trip through Serialize/DeserializeEntities, that's no longer true - only
			// the top-level "AudioData" key (an older, different shape) and AnimationComponent
			// remain genuinely unsupported.
			return entity["AudioData"] || entity["AnimationComponent"];
		}

		static std::string GetEntityNameForLog(const YAML::Node& entity, size_t entityIndex)
		{
			try
			{
				if (auto tag = entity["TagComponent"])
					return tag["Tag"].as<std::string>("Entity");
			}
			catch (const YAML::Exception&)
			{
			}

			return "entity index " + std::to_string(entityIndex);
		}

		static AssetHandle GetSerializableStaticMeshHandle(AssetHandle handle)
		{
			if (!handle || !Project::GetAssetManager() || !AssetManager::IsMemoryAsset(handle))
				return handle;

			Ref<Asset> asset = AssetManager::GetMemoryAsset(handle);
			if (!asset || asset->GetAssetType() != AssetType::StaticMesh)
				return handle;

			const AssetHandle meshSourceHandle = asset.As<StaticMesh>()->GetMeshSource();
			if (meshSourceHandle && !AssetManager::IsMemoryAsset(meshSourceHandle) && AssetManager::GetAssetType(meshSourceHandle) == AssetType::MeshSource)
				return meshSourceHandle;

			return handle;
		}

		static AssetHandle GetDefaultPrimitiveMeshSourceHandle(std::string_view primitiveName)
		{
			const char* filename = nullptr;
			if (primitiveName == "Capsule")
				filename = "Capsule.gltf";
			else if (primitiveName == "Cone")
				filename = "Cone.gltf";
			else if (primitiveName == "Cube")
				filename = "Cube.gltf";
			else if (primitiveName == "Cylinder")
				filename = "Cylinder.gltf";
			else if (primitiveName == "Plane")
				filename = "Plane.gltf";
			else if (primitiveName == "Sphere")
				filename = "Sphere.gltf";
			else if (primitiveName == "Torus")
				filename = "Torus.gltf";

			if (!filename)
				return 0;

			Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
			if (!editorAssetManager)
				return 0;

			const std::filesystem::path relativePath = std::filesystem::path("Meshes") / "Source" / "Default" / filename;
			AssetHandle handle = editorAssetManager->GetAssetHandleFromFilePath(relativePath);
			if (!handle || AssetManager::GetAssetType(handle) != AssetType::MeshSource)
				return 0;

			return handle;
		}

		static AssetHandle GetDeserializedStaticMeshHandle(const YAML::Node& staticMesh, Entity entity)
		{
			AssetHandle handle = staticMesh["AssetID"].as<uint64_t>(0);
			if (!handle || !Project::GetAssetManager() || AssetManager::IsAssetHandleValid(handle))
				return handle;

			const std::string& entityName = entity.GetComponent<TagComponent>().Tag;
			if (AssetHandle defaultPrimitive = GetDefaultPrimitiveMeshSourceHandle(entityName))
				return defaultPrimitive;

			return handle;
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
				const auto& tagComponent = entity.GetComponent<TagComponent>();
				out << YAML::Key << "Tag" << YAML::Value << tagComponent.Tag;
				// Only emitted when set, so existing scenes stay byte-identical.
				if (tagComponent.Locked)
					out << YAML::Key << "Locked" << YAML::Value << tagComponent.Locked;
				if (tagComponent.LabelColor != 0)
					out << YAML::Key << "LabelColor" << YAML::Value << tagComponent.LabelColor;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<FolderComponent>())
				out << YAML::Key << "Folder" << YAML::Value << true;

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
				out << YAML::Key << "ScriptID" << YAML::Value << (uint64_t)script.ScriptID;

				// Serialize the per-entity FieldStorage buffers (owned by the Scene).
				const ScriptStorage& storage = entity.GetScene()->GetScriptStorage();
				auto storageIt = storage.EntityStorage.find(entity.GetUUID());
				if (storageIt != storage.EntityStorage.end() && !storageIt->second.Fields.empty())
				{
					out << YAML::Key << "StoredFields" << YAML::Value << YAML::BeginSeq;
					for (const auto& [fieldID, fieldStorage] : storageIt->second.Fields)
					{
						out << YAML::BeginMap;
						out << YAML::Key << "ID" << YAML::Value << fieldID;
						out << YAML::Key << "Name" << YAML::Value << std::string(fieldStorage.GetName());
						out << YAML::Key << "Type" << YAML::Value << DataTypeToString(fieldStorage.GetType());

						if (fieldStorage.IsArray())
						{
							// Arrays serialize as a raw byte sequence to stay lossless.
							out << YAML::Key << "Array" << YAML::Value << true;
							out << YAML::Key << "Data" << YAML::Value << YAML::Flow << YAML::BeginSeq;
							const Buffer& buf = fieldStorage.ValueBuffer();
							for (uint64_t i = 0; i < buf.Size; i++)
								out << (uint32_t)((uint8_t*)buf.Data)[i];
							out << YAML::EndSeq;
						}
						else
						{
							out << YAML::Key << "Data" << YAML::Value;
							switch (fieldStorage.GetType())
							{
								case DataType::SByte:      out << (int32_t)fieldStorage.GetValue<int8_t>(); break;
								case DataType::Byte:       out << (uint32_t)fieldStorage.GetValue<uint8_t>(); break;
								case DataType::Short:      out << fieldStorage.GetValue<int16_t>(); break;
								case DataType::UShort:     out << fieldStorage.GetValue<uint16_t>(); break;
								case DataType::Int:        out << fieldStorage.GetValue<int32_t>(); break;
								case DataType::UInt:       out << fieldStorage.GetValue<uint32_t>(); break;
								case DataType::Long:       out << fieldStorage.GetValue<int64_t>(); break;
								case DataType::ULong:      out << fieldStorage.GetValue<uint64_t>(); break;
								case DataType::Float:      out << fieldStorage.GetValue<float>(); break;
								case DataType::Double:     out << fieldStorage.GetValue<double>(); break;
								case DataType::Vector2:    out << fieldStorage.GetValue<glm::vec2>(); break;
								case DataType::Vector3:    out << fieldStorage.GetValue<glm::vec3>(); break;
								case DataType::Vector4:    out << fieldStorage.GetValue<glm::vec4>(); break;
								case DataType::Bool:       out << (fieldStorage.GetValue<uint32_t>() != 0); break;
								default:                   out << (uint64_t)fieldStorage.GetValue<uint64_t>(); break; // Entity + asset-refs (UUID)
							}
						}
						out << YAML::EndMap;
					}
					out << YAML::EndSeq;
				}

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
				out << YAML::Key << "AssetID" << YAML::Value << GetSerializableStaticMeshHandle(staticMesh.StaticMesh);
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
				out << YAML::Key << "Unit" << YAML::Value << static_cast<uint32_t>(light.Unit);
				out << YAML::Key << "ColorTemperature" << YAML::Value << light.ColorTemperature;
				out << YAML::Key << "UseColorTemperature" << YAML::Value << light.UseColorTemperature;
				out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;
				out << YAML::Key << "SoftShadows" << YAML::Value << light.SoftShadows;
				out << YAML::Key << "LightSize" << YAML::Value << light.LightSize;
				out << YAML::Key << "ShadowAmount" << YAML::Value << light.ShadowAmount;
				out << YAML::Key << "ShadowDistance" << YAML::Value << light.ShadowDistance;
				out << YAML::Key << "ShadowResolutionTier" << YAML::Value << light.ShadowResolutionTier;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<PointLightComponent>())
			{
				const auto& light = entity.GetComponent<PointLightComponent>();
				out << YAML::Key << "PointLightComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Radiance" << YAML::Value << light.Radiance;
				out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
				out << YAML::Key << "Unit" << YAML::Value << static_cast<uint32_t>(light.Unit);
				out << YAML::Key << "ColorTemperature" << YAML::Value << light.ColorTemperature;
				out << YAML::Key << "UseColorTemperature" << YAML::Value << light.UseColorTemperature;
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
				out << YAML::Key << "Unit" << YAML::Value << static_cast<uint32_t>(light.Unit);
				out << YAML::Key << "ColorTemperature" << YAML::Value << light.ColorTemperature;
				out << YAML::Key << "UseColorTemperature" << YAML::Value << light.UseColorTemperature;
				out << YAML::Key << "Range" << YAML::Value << light.Range;
				out << YAML::Key << "ShadowDistance" << YAML::Value << light.ShadowDistance;
				out << YAML::Key << "ShadowResolutionTier" << YAML::Value << light.ShadowResolutionTier;
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

			if (entity.HasComponent<RigidBodyComponent>())
			{
				const auto& rb = entity.GetComponent<RigidBodyComponent>();
				out << YAML::Key << "RigidBodyComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "BodyType" << YAML::Value << (int)rb.BodyType;
				out << YAML::Key << "LayerID" << YAML::Value << rb.LayerID;
				out << YAML::Key << "EnableDynamicTypeChange" << YAML::Value << rb.EnableDynamicTypeChange;
				out << YAML::Key << "Mass" << YAML::Value << rb.Mass;
				out << YAML::Key << "LinearDrag" << YAML::Value << rb.LinearDrag;
				out << YAML::Key << "AngularDrag" << YAML::Value << rb.AngularDrag;
				out << YAML::Key << "DisableGravity" << YAML::Value << rb.DisableGravity;
				out << YAML::Key << "IsTrigger" << YAML::Value << rb.IsTrigger;
				out << YAML::Key << "CollisionDetection" << YAML::Value << (int)rb.CollisionDetection;
				out << YAML::Key << "InitialLinearVelocity" << YAML::Value << rb.InitialLinearVelocity;
				out << YAML::Key << "InitialAngularVelocity" << YAML::Value << rb.InitialAngularVelocity;
				out << YAML::Key << "MaxLinearVelocity" << YAML::Value << rb.MaxLinearVelocity;
				out << YAML::Key << "MaxAngularVelocity" << YAML::Value << rb.MaxAngularVelocity;
				out << YAML::Key << "LockedAxes" << YAML::Value << (uint32_t)rb.LockedAxes;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CharacterControllerComponent>())
			{
				const auto& controller = entity.GetComponent<CharacterControllerComponent>();
				out << YAML::Key << "CharacterControllerComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "SlopeLimitDeg" << YAML::Value << controller.SlopeLimitDeg;
				out << YAML::Key << "StepOffset" << YAML::Value << controller.StepOffset;
				out << YAML::Key << "LayerID" << YAML::Value << controller.LayerID;
				out << YAML::Key << "DisableGravity" << YAML::Value << controller.DisableGravity;
				out << YAML::Key << "ControlMovementInAir" << YAML::Value << controller.ControlMovementInAir;
				out << YAML::Key << "ControlRotationInAir" << YAML::Value << controller.ControlRotationInAir;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CompoundColliderComponent>())
			{
				const auto& compoundCollider = entity.GetComponent<CompoundColliderComponent>();
				out << YAML::Key << "CompoundColliderComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "IncludeStaticChildColliders" << YAML::Value << compoundCollider.IncludeStaticChildColliders;
				out << YAML::Key << "IsImmutable" << YAML::Value << compoundCollider.IsImmutable;
				out << YAML::Key << "CompoundedColliderEntities" << YAML::Value << YAML::BeginSeq;
				for (UUID entityID : compoundCollider.CompoundedColliderEntities)
					out << (uint64_t)entityID;
				out << YAML::EndSeq;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<BoxColliderComponent>())
			{
				const auto& collider = entity.GetComponent<BoxColliderComponent>();
				out << YAML::Key << "BoxColliderComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "HalfSize" << YAML::Value << collider.HalfSize;
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Density" << YAML::Value << collider.Material.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Material.Friction;
				out << YAML::Key << "Restitution" << YAML::Value << collider.Material.Restitution;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<SphereColliderComponent>())
			{
				const auto& collider = entity.GetComponent<SphereColliderComponent>();
				out << YAML::Key << "SphereColliderComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Radius" << YAML::Value << collider.Radius;
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Density" << YAML::Value << collider.Material.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Material.Friction;
				out << YAML::Key << "Restitution" << YAML::Value << collider.Material.Restitution;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<CapsuleColliderComponent>())
			{
				const auto& collider = entity.GetComponent<CapsuleColliderComponent>();
				out << YAML::Key << "CapsuleColliderComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Radius" << YAML::Value << collider.Radius;
				out << YAML::Key << "HalfHeight" << YAML::Value << collider.HalfHeight;
				out << YAML::Key << "Offset" << YAML::Value << collider.Offset;
				out << YAML::Key << "Density" << YAML::Value << collider.Material.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Material.Friction;
				out << YAML::Key << "Restitution" << YAML::Value << collider.Material.Restitution;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<MeshColliderComponent>())
			{
				const auto& collider = entity.GetComponent<MeshColliderComponent>();
				out << YAML::Key << "MeshColliderComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "ColliderAsset" << YAML::Value << collider.ColliderAsset;
				out << YAML::Key << "SubmeshIndex" << YAML::Value << collider.SubmeshIndex;
				out << YAML::Key << "UseSharedShape" << YAML::Value << collider.UseSharedShape;
				out << YAML::Key << "Density" << YAML::Value << collider.Material.Density;
				out << YAML::Key << "Friction" << YAML::Value << collider.Material.Friction;
				out << YAML::Key << "Restitution" << YAML::Value << collider.Material.Restitution;
				out << YAML::Key << "AcousticMaterial" << YAML::Value << AcousticMaterialName(collider.Acoustic);
				out << YAML::Key << "AcousticMotion" << YAML::Value << static_cast<uint32_t>(collider.AcousticMotion);
				out << YAML::Key << "CollisionComplexity" << YAML::Value << (uint8_t)collider.CollisionComplexity;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<MusicDirectorComponent>())
			{
				const auto& music = entity.GetComponent<MusicDirectorComponent>();
				out << YAML::Key << "MusicDirectorComponent" << YAML::BeginMap;
				out << YAML::Key << "Event" << YAML::BeginMap;
				out << YAML::Key << "Guid" << YAML::Value << music.Event.Guid;
				out << YAML::Key << "Path" << YAML::Value << music.Event.Path;
				out << YAML::Key << "BankName" << YAML::Value << music.Event.BankName;
				out << YAML::EndMap;
				out << YAML::Key << "PlayOnAwake" << YAML::Value << music.PlayOnAwake;
				out << YAML::Key << "InitialState" << YAML::Value << music.InitialState;
				out << YAML::Key << "Intensity" << YAML::Value << music.Intensity;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<AudioZoneComponent>())
			{
				const auto& zone = entity.GetComponent<AudioZoneComponent>();
				out << YAML::Key << "AudioZoneComponent" << YAML::BeginMap;
				out << YAML::Key << "Shape" << YAML::Value << static_cast<uint32_t>(zone.Shape);
				out << YAML::Key << "Enabled" << YAML::Value << zone.Enabled;
				out << YAML::Key << "Offset" << YAML::Value << zone.Offset;
				out << YAML::Key << "HalfExtents" << YAML::Value << zone.HalfExtents;
				out << YAML::Key << "Radius" << YAML::Value << zone.Radius;
				out << YAML::Key << "Priority" << YAML::Value << zone.Priority;
				out << YAML::Key << "BlendDistance" << YAML::Value << zone.BlendDistance;
				out << YAML::Key << "FadeTime" << YAML::Value << zone.FadeTime;
				out << YAML::Key << "Volume" << YAML::Value << zone.Volume;
				out << YAML::Key << "AmbienceEvent" << YAML::BeginMap;
				out << YAML::Key << "Guid" << YAML::Value << zone.AmbienceEvent.Guid;
				out << YAML::Key << "Path" << YAML::Value << zone.AmbienceEvent.Path;
				out << YAML::Key << "BankName" << YAML::Value << zone.AmbienceEvent.BankName;
				out << YAML::EndMap;
				out << YAML::Key << "Snapshot" << YAML::BeginMap;
				out << YAML::Key << "Guid" << YAML::Value << zone.Snapshot.Guid;
				out << YAML::Key << "Path" << YAML::Value << zone.Snapshot.Path;
				out << YAML::Key << "BankName" << YAML::Value << zone.Snapshot.BankName;
				out << YAML::EndMap;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<AudioPortalComponent>())
			{
				const auto& portal = entity.GetComponent<AudioPortalComponent>();
				out << YAML::Key << "AudioPortalComponent" << YAML::BeginMap;
				out << YAML::Key << "Enabled" << YAML::Value << portal.Enabled;
				out << YAML::Key << "ZoneA" << YAML::Value << static_cast<uint64_t>(portal.ZoneA);
				out << YAML::Key << "ZoneB" << YAML::Value << static_cast<uint64_t>(portal.ZoneB);
				out << YAML::Key << "HalfExtents" << YAML::Value << portal.HalfExtents;
				out << YAML::Key << "Open" << YAML::Value << portal.Open;
				out << YAML::Key << "BlendDistance" << YAML::Value << portal.BlendDistance;
				out << YAML::Key << "Material" << YAML::Value << AcousticMaterialName(portal.Material);
				out << YAML::EndMap;
			}

			if (entity.HasComponent<AudioSurfaceComponent>())
			{
				out << YAML::Key << "AudioSurfaceComponent" << YAML::BeginMap;
				const auto& surface = entity.GetComponent<AudioSurfaceComponent>();
				out << YAML::Key << "Material" << YAML::Value << AcousticMaterialName(surface.Material);
				out << YAML::Key << "PhysicsSounds" << YAML::Value << surface.PhysicsSounds;
				out << YAML::Key << "AutoFootsteps" << YAML::Value << surface.AutoFootsteps;
				out << YAML::Key << "StrideLength" << YAML::Value << surface.StrideLength;
				out << YAML::Key << "GroundProbeDistance" << YAML::Value << surface.GroundProbeDistance;
				out << YAML::Key << "FootstepWeight" << YAML::Value << surface.FootstepWeight;
				auto writeEvent = [&](const char* name, const AudioEventRef& reference)
				{
					out << YAML::Key << name << YAML::BeginMap;
					out << YAML::Key << "Guid" << YAML::Value << reference.Guid;
					out << YAML::Key << "Path" << YAML::Value << reference.Path;
					out << YAML::Key << "BankName" << YAML::Value << reference.BankName << YAML::EndMap;
				};
				writeEvent("FootstepOverride", surface.FootstepOverride);
				writeEvent("ImpactOverride", surface.ImpactOverride);
				out << YAML::EndMap;
			}

			if (entity.HasComponent<AudioSourceComponent>())
			{
				const auto& audioSource = entity.GetComponent<AudioSourceComponent>();
				const auto& config = audioSource.Config;
				out << YAML::Key << "AudioSourceComponent";
				out << YAML::BeginMap;
				if (audioSource.LegacyAudio || audioSource.LegacyLooping)
				{
					out << YAML::Key << "Audio" << YAML::Value << audioSource.LegacyAudio;
					out << YAML::Key << "Looping" << YAML::Value << audioSource.LegacyLooping;
				}
				out << YAML::Key << "VolumeMultiplier" << YAML::Value << config.VolumeMultiplier;
				out << YAML::Key << "PitchMultiplier" << YAML::Value << config.PitchMultiplier;
				out << YAML::Key << "PlayOnAwake" << YAML::Value << config.PlayOnAwake;

				out << YAML::Key << "EventGuid" << YAML::Value << audioSource.Event.Guid;
				out << YAML::Key << "EventPath" << YAML::Value << audioSource.Event.Path;
				out << YAML::Key << "EventBank" << YAML::Value << audioSource.Event.BankName;
				out << YAML::Key << "ParameterOverrides" << YAML::Value << YAML::BeginSeq;
				for (const auto& [name, value] : audioSource.ParameterOverrides)
				{
					out << YAML::BeginMap;
					out << YAML::Key << "Name" << YAML::Value << name;
					out << YAML::Key << "Value" << YAML::Value << value;
					out << YAML::EndMap;
				}
				out << YAML::EndSeq;
				out << YAML::EndMap;
			}

			if (entity.HasComponent<AudioListenerComponent>())
			{
				const auto& listener = entity.GetComponent<AudioListenerComponent>();
				out << YAML::Key << "AudioListenerComponent";
				out << YAML::BeginMap;
				out << YAML::Key << "Active" << YAML::Value << listener.Active;
				out << YAML::Key << "ListenerIndex" << YAML::Value << listener.ListenerIndex;
				out << YAML::Key << "Weight" << YAML::Value << listener.Weight;
				out << YAML::Key << "UseAttenuationTarget" << YAML::Value << listener.UseAttenuationTarget;
				out << YAML::Key << "AttenuationTarget" << YAML::Value << listener.AttenuationTarget;
				out << YAML::EndMap;
			}

			out << YAML::EndMap;
		}

		static bool DeserializeEntities(const YAML::Node& entities, Ref<Scene> scene)
		{
			if (!entities)
				return true;

			if (!entities.IsSequence())
			{
				LUX_CORE_ERROR("Scene has an invalid Entities block; expected a YAML sequence.");
				return false;
			}

			size_t entityIndex = 0;
			for (auto entity : entities)
			{
				try
				{
					if (!entity || !entity.IsMap())
					{
						LUX_CORE_ERROR("Scene contains an invalid entity at index {0}; expected a YAML map.", entityIndex);
						return false;
					}

					if (!entity["Entity"])
					{
						LUX_CORE_ERROR("Scene contains an entity without an Entity UUID at index {0}.", entityIndex);
						return false;
					}

					if (ContainsLegacyOrDeferredSceneData(entity))
					{
						LUX_CORE_ERROR("Scene contains unsupported legacy/deferred component data on {0}; refusing to load incompatible scene schema.", GetEntityNameForLog(entity, entityIndex));
						return false;
					}

					const UUID uuid = entity["Entity"].as<uint64_t>();
					std::string name = "Entity";
					if (auto tag = entity["TagComponent"])
						name = tag["Tag"].as<std::string>("Entity");

					scene->CreateEntityWithID(uuid, name, false);
				}
				catch (const YAML::Exception& e)
				{
					LUX_CORE_ERROR("Failed to deserialize scene entity header at index {0}: {1}", entityIndex, e.what());
					return false;
				}
				catch (const std::exception& e)
				{
					LUX_CORE_ERROR("Failed to deserialize scene entity header at index {0}: {1}", entityIndex, e.what());
					return false;
				}

				entityIndex++;
			}

			entityIndex = 0;
			for (auto entity : entities)
			{
				try
				{
				Entity deserializedEntity = scene->GetEntityWithUUID(entity["Entity"].as<uint64_t>());

				// Editor lock/label state lives on the (already-created) TagComponent; absent keys
				// keep the defaults so older scenes load unchanged.
				if (auto tag = entity["TagComponent"])
				{
					auto& tagComponent = deserializedEntity.GetComponent<TagComponent>();
					tagComponent.Locked = tag["Locked"].as<bool>(false);
					tagComponent.LabelColor = tag["LabelColor"].as<uint32_t>(0);
				}

				if (entity["Folder"] && entity["Folder"].as<bool>(false))
					deserializedEntity.AddComponent<FolderComponent>();

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
					component.ScriptID = script["ScriptID"].as<uint64_t>(
						component.ClassName.empty() ? 0 : (uint64_t)Hash::GenerateFNVHash(component.ClassName));

					const auto& scriptEngine = ScriptEngine::GetInstance();
					UUID entityID = deserializedEntity.GetUUID();

					if (scriptEngine.IsValidScript(component.ScriptID))
					{
						auto& storage = scene->GetScriptStorage();
						if (!storage.EntityStorage.contains(entityID))
							storage.InitializeEntityStorage(component.ScriptID, entityID);
						auto& entityStorage = storage.EntityStorage.at(entityID);

						if (auto storedFields = script["StoredFields"])
						{
							for (auto sf : storedFields)
							{
								uint32_t fieldID = sf["ID"].as<uint32_t>(0);
								DataType type = DataTypeFromString(sf["Type"].as<std::string>("Float"));
								auto fieldIt = entityStorage.Fields.find(fieldID);
								if (fieldIt == entityStorage.Fields.end())
									continue;

								FieldStorage& fs = fieldIt->second;
								auto dataNode = sf["Data"];

								if (sf["Array"] && sf["Array"].as<bool>(false))
								{
									size_t n = dataNode.size();
									Buffer& buf = fs.ValueBuffer();
									buf.Allocate(n);
									for (size_t i = 0; i < n; i++)
										((uint8_t*)buf.Data)[i] = (uint8_t)dataNode[i].as<uint32_t>(0);
									continue;
								}

								switch (type)
								{
									case DataType::SByte:   fs.SetValue<int8_t>((int8_t)dataNode.as<int32_t>(0)); break;
									case DataType::Byte:    fs.SetValue<uint8_t>((uint8_t)dataNode.as<uint32_t>(0)); break;
									case DataType::Short:   fs.SetValue<int16_t>(dataNode.as<int16_t>(0)); break;
									case DataType::UShort:  fs.SetValue<uint16_t>(dataNode.as<uint16_t>(0)); break;
									case DataType::Int:     fs.SetValue<int32_t>(dataNode.as<int32_t>(0)); break;
									case DataType::UInt:    fs.SetValue<uint32_t>(dataNode.as<uint32_t>(0)); break;
									case DataType::Long:    fs.SetValue<int64_t>(dataNode.as<int64_t>(0)); break;
									case DataType::ULong:   fs.SetValue<uint64_t>(dataNode.as<uint64_t>(0)); break;
									case DataType::Float:   fs.SetValue<float>(dataNode.as<float>(0.0f)); break;
									case DataType::Double:  fs.SetValue<double>(dataNode.as<double>(0.0)); break;
									case DataType::Vector2: fs.SetValue<glm::vec2>(dataNode.as<glm::vec2>(glm::vec2(0.0f))); break;
									case DataType::Vector3: fs.SetValue<glm::vec3>(dataNode.as<glm::vec3>(glm::vec3(0.0f))); break;
									case DataType::Vector4: fs.SetValue<glm::vec4>(dataNode.as<glm::vec4>(glm::vec4(0.0f))); break;
									case DataType::Bool:    fs.SetValue<uint32_t>(dataNode.as<bool>(false) ? 1u : 0u); break;
									default:                fs.SetValue<uint64_t>(dataNode.as<uint64_t>(0)); break; // Entity + asset-refs
								}
							}
						}
					}
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
					component.StaticMesh = GetDeserializedStaticMeshHandle(staticMesh, deserializedEntity);
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
					component.Unit = static_cast<LightUnit>(light["Unit"].as<uint32_t>(static_cast<uint32_t>(LightUnit::Unitless)));
					component.ColorTemperature = light["ColorTemperature"].as<float>(6500.0f);
					component.UseColorTemperature = light["UseColorTemperature"].as<bool>(false);
					component.CastShadows = light["CastShadows"].as<bool>(true);
					component.SoftShadows = light["SoftShadows"].as<bool>(true);
					component.LightSize = light["LightSize"].as<float>(0.5f);
					component.ShadowAmount = light["ShadowAmount"].as<float>(1.0f);
					component.ShadowDistance = light["ShadowDistance"].as<float>(0.0f);
					component.ShadowResolutionTier = light["ShadowResolutionTier"].as<uint32_t>(2);
				}

				if (auto light = entity["PointLightComponent"])
				{
					auto& component = deserializedEntity.AddComponent<PointLightComponent>();
					component.Radiance = light["Radiance"].as<glm::vec3>(glm::vec3(1.0f));
					component.Intensity = light["Intensity"].as<float>(1.0f);
					component.Unit = static_cast<LightUnit>(light["Unit"].as<uint32_t>(static_cast<uint32_t>(LightUnit::Unitless)));
					component.ColorTemperature = light["ColorTemperature"].as<float>(6500.0f);
					component.UseColorTemperature = light["UseColorTemperature"].as<bool>(false);
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
					component.Unit = static_cast<LightUnit>(light["Unit"].as<uint32_t>(static_cast<uint32_t>(LightUnit::Unitless)));
					component.ColorTemperature = light["ColorTemperature"].as<float>(6500.0f);
					component.UseColorTemperature = light["UseColorTemperature"].as<bool>(false);
					component.Range = light["Range"].as<float>(10.0f);
					component.Angle = light["Angle"].as<float>(60.0f);
					component.AngleAttenuation = light["AngleAttenuation"].as<float>(5.0f);
					component.CastsShadows = light["CastsShadows"].as<bool>(false);
					component.SoftShadows = light["SoftShadows"].as<bool>(false);
					component.Falloff = light["Falloff"].as<float>(1.0f);
					component.ShadowDistance = light["ShadowDistance"].as<float>(0.0f);
					component.ShadowResolutionTier = light["ShadowResolutionTier"].as<uint32_t>(1);
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

				if (auto rigidBody = entity["RigidBodyComponent"])
				{
					auto& component = deserializedEntity.AddComponent<RigidBodyComponent>();
					component.BodyType = (EBodyType)rigidBody["BodyType"].as<int>((int)EBodyType::Static);
					component.LayerID = rigidBody["LayerID"].as<uint32_t>(0);
					component.EnableDynamicTypeChange = rigidBody["EnableDynamicTypeChange"].as<bool>(false);
					component.Mass = rigidBody["Mass"].as<float>(1.0f);
					component.LinearDrag = rigidBody["LinearDrag"].as<float>(0.01f);
					component.AngularDrag = rigidBody["AngularDrag"].as<float>(0.05f);
					component.DisableGravity = rigidBody["DisableGravity"].as<bool>(false);
					component.IsTrigger = rigidBody["IsTrigger"].as<bool>(false);
					component.CollisionDetection = (ECollisionDetectionType)rigidBody["CollisionDetection"].as<int>((int)ECollisionDetectionType::Discrete);
					component.InitialLinearVelocity = rigidBody["InitialLinearVelocity"].as<glm::vec3>(glm::vec3(0.0f));
					component.InitialAngularVelocity = rigidBody["InitialAngularVelocity"].as<glm::vec3>(glm::vec3(0.0f));
					component.MaxLinearVelocity = rigidBody["MaxLinearVelocity"].as<float>(500.0f);
					component.MaxAngularVelocity = rigidBody["MaxAngularVelocity"].as<float>(50.0f);
					component.LockedAxes = (EActorAxis)rigidBody["LockedAxes"].as<uint32_t>((uint32_t)EActorAxis::None);
				}

				if (auto characterController = entity["CharacterControllerComponent"])
				{
					auto& component = deserializedEntity.AddComponent<CharacterControllerComponent>();
					component.SlopeLimitDeg = characterController["SlopeLimitDeg"].as<float>(45.0f);
					component.StepOffset = characterController["StepOffset"].as<float>(0.5f);
					component.LayerID = characterController["LayerID"].as<uint32_t>(0);
					component.DisableGravity = characterController["DisableGravity"].as<bool>(false);
					component.ControlMovementInAir = characterController["ControlMovementInAir"].as<bool>(false);
					component.ControlRotationInAir = characterController["ControlRotationInAir"].as<bool>(false);
				}

				if (auto compoundCollider = entity["CompoundColliderComponent"])
				{
					auto& component = deserializedEntity.AddComponent<CompoundColliderComponent>();
					component.IncludeStaticChildColliders = compoundCollider["IncludeStaticChildColliders"].as<bool>(true);
					component.IsImmutable = compoundCollider["IsImmutable"].as<bool>(true);
					if (auto compoundedEntities = compoundCollider["CompoundedColliderEntities"])
					{
						for (auto compoundedEntity : compoundedEntities)
							component.CompoundedColliderEntities.emplace_back(compoundedEntity.as<uint64_t>(0));
					}
				}

				if (auto boxCollider = entity["BoxColliderComponent"])
				{
					auto& component = deserializedEntity.AddComponent<BoxColliderComponent>();
					component.HalfSize = boxCollider["HalfSize"].as<glm::vec3>(glm::vec3{ 0.5f, 0.5f, 0.5f });
					component.Offset = boxCollider["Offset"].as<glm::vec3>(glm::vec3{ 0.0f, 0.0f, 0.0f });
					component.Material.Density = boxCollider["Density"].as<float>(1.0f);
					component.Material.Friction = boxCollider["Friction"].as<float>(0.5f);
					component.Material.Restitution = boxCollider["Restitution"].as<float>(0.0f);
				}

				if (auto sphereCollider = entity["SphereColliderComponent"])
				{
					auto& component = deserializedEntity.AddComponent<SphereColliderComponent>();
					component.Radius = sphereCollider["Radius"].as<float>(0.5f);
					component.Offset = sphereCollider["Offset"].as<glm::vec3>(glm::vec3{ 0.0f, 0.0f, 0.0f });
					component.Material.Density = sphereCollider["Density"].as<float>(1.0f);
					component.Material.Friction = sphereCollider["Friction"].as<float>(0.5f);
					component.Material.Restitution = sphereCollider["Restitution"].as<float>(0.0f);
				}

				if (auto capsuleCollider = entity["CapsuleColliderComponent"])
				{
					auto& component = deserializedEntity.AddComponent<CapsuleColliderComponent>();
					component.Radius = capsuleCollider["Radius"].as<float>(0.5f);
					component.HalfHeight = capsuleCollider["HalfHeight"].as<float>(0.5f);
					component.Offset = capsuleCollider["Offset"].as<glm::vec3>(glm::vec3{ 0.0f, 0.0f, 0.0f });
					component.Material.Density = capsuleCollider["Density"].as<float>(1.0f);
					component.Material.Friction = capsuleCollider["Friction"].as<float>(0.5f);
					component.Material.Restitution = capsuleCollider["Restitution"].as<float>(0.0f);
				}

				if (auto meshCollider = entity["MeshColliderComponent"])
				{
					auto& component = deserializedEntity.AddComponent<MeshColliderComponent>();
					component.ColliderAsset = meshCollider["ColliderAsset"].as<uint64_t>(0);
					component.SubmeshIndex = meshCollider["SubmeshIndex"].as<uint32_t>(0);
					component.UseSharedShape = meshCollider["UseSharedShape"].as<bool>(false);
					const auto motion = meshCollider["AcousticMotion"] ? meshCollider["AcousticMotion"].as<uint32_t>() : 0;
					if (motion > static_cast<uint32_t>(AcousticGeometryMode::Disabled))
						throw std::runtime_error("Invalid mesh acoustic motion mode");
					component.AcousticMotion = static_cast<AcousticGeometryMode>(motion);
					component.Material.Density = meshCollider["Density"].as<float>(1.0f);
					component.Material.Friction = meshCollider["Friction"].as<float>(0.5f);
					component.Material.Restitution = meshCollider["Restitution"].as<float>(0.0f);
					if (!ParseAcousticMaterial(meshCollider["AcousticMaterial"].as<std::string>("Default"), component.Acoustic))
						throw std::runtime_error("Invalid mesh collider acoustic material");
					component.CollisionComplexity = (ECollisionComplexity)meshCollider["CollisionComplexity"].as<uint8_t>((uint8_t)ECollisionComplexity::Default);
				}

				if (auto node = entity["MusicDirectorComponent"])
				{
					auto& music = deserializedEntity.AddComponent<MusicDirectorComponent>();
					if (auto reference = node["Event"])
					{
						music.Event.Guid = reference["Guid"].as<std::string>("");
						music.Event.Path = reference["Path"].as<std::string>("");
						music.Event.BankName = reference["BankName"].as<std::string>("");
					}
					music.PlayOnAwake = node["PlayOnAwake"].as<bool>(music.PlayOnAwake);
					music.InitialState = node["InitialState"].as<std::string>(music.InitialState);
					music.Intensity = node["Intensity"].as<float>(music.Intensity);
					if (!std::isfinite(music.Intensity) || music.Intensity < 0.0f || music.Intensity > 1.0f)
						throw std::runtime_error("Music intensity must be finite and in [0, 1]");
				}

				if (auto node = entity["AudioZoneComponent"])
				{
					auto& zone = deserializedEntity.AddComponent<AudioZoneComponent>();
					const auto shape = node["Shape"].as<uint32_t>(0);
					if (shape > static_cast<uint32_t>(AudioZoneShape::Collider))
						throw std::runtime_error("Invalid audio zone shape");
					zone.Shape = static_cast<AudioZoneShape>(shape);
					zone.Enabled = node["Enabled"].as<bool>(zone.Enabled);
					zone.Offset = node["Offset"].as<glm::vec3>(zone.Offset);
					zone.HalfExtents = node["HalfExtents"].as<glm::vec3>(zone.HalfExtents);
					zone.Radius = node["Radius"].as<float>(zone.Radius);
					zone.Priority = node["Priority"].as<float>(zone.Priority);
					zone.BlendDistance = node["BlendDistance"].as<float>(zone.BlendDistance);
					zone.FadeTime = node["FadeTime"].as<float>(zone.FadeTime);
					zone.Volume = node["Volume"].as<float>(zone.Volume);
					if (auto reference = node["AmbienceEvent"])
					{
						zone.AmbienceEvent.Guid = reference["Guid"].as<std::string>("");
						zone.AmbienceEvent.Path = reference["Path"].as<std::string>("");
						zone.AmbienceEvent.BankName = reference["BankName"].as<std::string>("");
					}
					if (auto reference = node["Snapshot"])
					{
						zone.Snapshot.Guid = reference["Guid"].as<std::string>("");
						zone.Snapshot.Path = reference["Path"].as<std::string>("");
						zone.Snapshot.BankName = reference["BankName"].as<std::string>("");
					}
					if (!AudioZoneSystem::Validate(zone))
						throw std::runtime_error("Invalid audio zone dimensions or blend settings");
				}

				if (auto node = entity["AudioPortalComponent"])
				{
					auto& portal = deserializedEntity.AddComponent<AudioPortalComponent>();
					if (node["Enabled"])
						portal.Enabled = node["Enabled"].as<bool>();
					if (node["ZoneA"])
						portal.ZoneA = node["ZoneA"].as<uint64_t>();
					if (node["ZoneB"])
						portal.ZoneB = node["ZoneB"].as<uint64_t>();
					if (node["HalfExtents"])
						portal.HalfExtents = node["HalfExtents"].as<glm::vec3>();
					if (node["Open"])
						portal.Open = node["Open"].as<float>();
					if (node["BlendDistance"])
						portal.BlendDistance = node["BlendDistance"].as<float>();
					if (!ParseAcousticMaterial((node["Material"] ? node["Material"].as<std::string>() : "Wood"), portal.Material) || !AudioZoneSystem::Validate(portal))
						throw std::runtime_error("Invalid audio portal material, dimensions or open factor");
				}

				if (auto surface = entity["AudioSurfaceComponent"])
				{
					auto& component = deserializedEntity.AddComponent<AudioSurfaceComponent>();
					if (!ParseAcousticMaterial(surface["Material"].as<std::string>("Default"), component.Material))
						throw std::runtime_error("Invalid audio surface material");
					component.PhysicsSounds = surface["PhysicsSounds"].as<bool>(true);
					component.AutoFootsteps = surface["AutoFootsteps"].as<bool>(false);
					component.StrideLength = surface["StrideLength"].as<float>(0.7f);
					component.GroundProbeDistance = surface["GroundProbeDistance"].as<float>(1.2f);
					component.FootstepWeight = surface["FootstepWeight"].as<float>(75.0f);
					if (!std::isfinite(component.StrideLength) || component.StrideLength <= 0.0f
						|| !std::isfinite(component.GroundProbeDistance) || component.GroundProbeDistance <= 0.0f
						|| !std::isfinite(component.FootstepWeight) || component.FootstepWeight <= 0.0f)
						throw std::runtime_error("Invalid footstep settings");
					auto readEvent = [&](const char* name)
					{
						const auto value = surface[name];
						return value ? AudioEventRef{ value["Guid"].as<std::string>(""), value["Path"].as<std::string>(""), value["BankName"].as<std::string>("") } : AudioEventRef{};
					};
					component.FootstepOverride = readEvent("FootstepOverride");
					component.ImpactOverride = readEvent("ImpactOverride");
				}

				if (auto audioSource = entity["AudioSourceComponent"])
				{
					auto& component = deserializedEntity.AddComponent<AudioSourceComponent>();
					component.LegacyAudio = audioSource["Audio"].as<uint64_t>(0);

					auto& config = component.Config;
					config.VolumeMultiplier = audioSource["VolumeMultiplier"].as<float>(1.0f);
					config.PitchMultiplier = audioSource["PitchMultiplier"].as<float>(1.0f);
					config.PlayOnAwake = audioSource["PlayOnAwake"].as<bool>(true);
					component.LegacyLooping = audioSource["Looping"].as<bool>(false);

					// Spatialization, AttenuationModel, RollOff, Min/MaxGain, Min/MaxDistance, the
					// cone angles and DopplerFactor were removed: an FMOD Studio event authors all
					// of them. Scenes saved before that still carry the keys, and they are ignored
					// here deliberately rather than by accident - reading them would resurrect
					// settings that no longer reach the mixer.

					if (auto overrides = audioSource["ParameterOverrides"])
					{
						for (auto entry : overrides)
						{
							component.ParameterOverrides.emplace_back(
								entry["Name"].as<std::string>(std::string{}),
								entry["Value"].as<float>(0.0f));
						}
					}

					component.Event.Guid = audioSource["EventGuid"].as<std::string>(std::string{});
					component.Event.Path = audioSource["EventPath"].as<std::string>(std::string{});
					component.Event.BankName = audioSource["EventBank"].as<std::string>(std::string{});
				}

				if (auto audioListener = entity["AudioListenerComponent"])
				{
					auto& component = deserializedEntity.AddComponent<AudioListenerComponent>();
					component.Active = audioListener["Active"].as<bool>(true);
					// Legacy cone keys are intentionally ignored; old scenes retain listener 0.
					component.ListenerIndex = audioListener["ListenerIndex"].as<int>(0);
					component.Weight = audioListener["Weight"].as<float>(1.0f);
					component.UseAttenuationTarget = audioListener["UseAttenuationTarget"].as<bool>(false);
					component.AttenuationTarget = audioListener["AttenuationTarget"].as<uint64_t>(0);
				}
				}
				catch (const YAML::Exception& e)
				{
					LUX_CORE_ERROR("Failed to deserialize scene components for {0}: {1}", GetEntityNameForLog(entity, entityIndex), e.what());
					return false;
				}
				catch (const std::exception& e)
				{
					LUX_CORE_ERROR("Failed to deserialize scene components for {0}: {1}", GetEntityNameForLog(entity, entityIndex), e.what());
					return false;
				}

				entityIndex++;
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

		// Scene-wide post-processing. Previously authored per PostProcessVolume entity; the
		// volume system is gone, so it lives here as one block.
		{
			const PostProcessSettings& post = m_Scene->GetPostProcessSettings();
			out << YAML::Key << "PostProcess" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "Exposure" << YAML::Value << post.Exposure;
			out << YAML::Key << "ExposureMode" << YAML::Value << static_cast<uint32_t>(post.ExposureControl);
			out << YAML::Key << "Aperture" << YAML::Value << post.Aperture;
			out << YAML::Key << "ShutterSpeed" << YAML::Value << post.ShutterSpeed;
			out << YAML::Key << "ISO" << YAML::Value << post.ISO;
			out << YAML::Key << "ExposureEV100" << YAML::Value << post.ExposureEV100;
			out << YAML::Key << "ExposureCompensation" << YAML::Value << post.ExposureCompensation;
			out << YAML::Key << "AutoMinEV100" << YAML::Value << post.AutoMinEV100;
			out << YAML::Key << "AutoMaxEV100" << YAML::Value << post.AutoMaxEV100;
			out << YAML::Key << "AutoAdaptationSpeedUp" << YAML::Value << post.AutoAdaptationSpeedUp;
			out << YAML::Key << "AutoAdaptationSpeedDown" << YAML::Value << post.AutoAdaptationSpeedDown;
			out << YAML::Key << "ColorFilter" << YAML::Value << post.ColorFilter;
			out << YAML::Key << "Saturation" << YAML::Value << post.Saturation;
			out << YAML::Key << "Contrast" << YAML::Value << post.Contrast;
			out << YAML::Key << "Gamma" << YAML::Value << post.Gamma;
			out << YAML::Key << "Tonemap" << YAML::Value << static_cast<uint32_t>(post.Tonemap);
			out << YAML::Key << "WhiteTemperature" << YAML::Value << post.WhiteTemperature;
			out << YAML::Key << "WhiteTint" << YAML::Value << post.WhiteTint;
			out << YAML::Key << "Lift" << YAML::Value << post.Lift;
			out << YAML::Key << "GradeGamma" << YAML::Value << post.GradeGamma;
			out << YAML::Key << "Gain" << YAML::Value << post.Gain;
			out << YAML::EndMap;
		}

		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

		auto view = m_Scene->m_Registry.view<IDComponent>();
		for (auto entityID : view)
			SerializeEntity(out, { entityID, m_Scene.get() });

		out << YAML::EndSeq;
		out << YAML::EndMap;
	}

	std::string SceneSerializer::SerializeToString()
	{
		YAML::Emitter out;
		SerializeToYAML(out);
		return std::string(out.c_str());
	}

	std::map<UUID, std::string> SceneSerializer::SerializeEntitySnapshots(std::string& outMeta)
	{
		// Round-trip the whole scene through YAML once, then split it: each entity block keyed by
		// UUID, and the scene metadata (everything except Entities). Emitting from the parsed nodes
		// makes the output stable across calls, which is what the undo diff relies on.
		std::map<UUID, std::string> snapshots;

		const std::string full = SerializeToString();
		YAML::Node root = YAML::Load(full);

		if (root["Entities"] && root["Entities"].IsSequence())
		{
			for (const auto& entityNode : root["Entities"])
			{
				if (!entityNode["Entity"])
					continue;

				const UUID uuid = entityNode["Entity"].as<uint64_t>();
				YAML::Emitter entityOut;
				entityOut << entityNode;
				snapshots[uuid] = entityOut.c_str();
			}
		}

		// Metadata = the scene with an emptied Entities list.
		root.remove("Entities");
		YAML::Emitter metaOut;
		metaOut << root;
		outMeta = metaOut.c_str();

		return snapshots;
	}

	std::unordered_set<std::string> SceneSerializer::GetOverriddenComponentKeys(Entity instance, Entity prefabSource)
	{
		std::unordered_set<std::string> result;
		if (!instance || !prefabSource)
			return result;

		// Identity/hierarchy keys always differ between an instance and its source — never overrides.
		static const std::unordered_set<std::string> ignored = {
			"Entity", "Parent", "Children", "PrefabComponent", "TagComponent"
		};

		YAML::Emitter instOut, srcOut;
		SerializeEntity(instOut, instance);
		SerializeEntity(srcOut, prefabSource);
		const YAML::Node instNode = YAML::Load(instOut.c_str());
		YAML::Node srcNode = YAML::Load(srcOut.c_str());
		if (prefabSource.HasComponent<AudioListenerComponent>())
		{
			// Compare in the instance's UUID space; remapped references are not user overrides.
			srcNode["AudioListenerComponent"]["AttenuationTarget"] = static_cast<uint64_t>(Scene::MapPrefabEntityReference(
				prefabSource.GetComponent<AudioListenerComponent>().AttenuationTarget, prefabSource, instance));
		}

		if (prefabSource.HasComponent<AudioPortalComponent>())
		{
			const auto& portal = prefabSource.GetComponent<AudioPortalComponent>();
			srcNode["AudioPortalComponent"]["ZoneA"] = static_cast<uint64_t>(Scene::MapPrefabEntityReference(portal.ZoneA, prefabSource, instance));
			srcNode["AudioPortalComponent"]["ZoneB"] = static_cast<uint64_t>(Scene::MapPrefabEntityReference(portal.ZoneB, prefabSource, instance));
		}

		// A key is an override if it is present on one side only, or present on both but serializes
		// differently. Scanning both directions catches instance-only and prefab-only components.
		const auto scan = [&](const YAML::Node& lhs, const YAML::Node& rhs)
		{
			for (auto it = lhs.begin(); it != lhs.end(); ++it)
			{
				const std::string key = it->first.as<std::string>();
				if (ignored.contains(key))
					continue;

				const YAML::Node other = rhs[key];
				if (!other.IsDefined() || YAML::Dump(it->second) != YAML::Dump(other))
					result.insert(key);
			}
		};
		scan(instNode, srcNode);
		scan(srcNode, instNode);

		return result;
	}

	bool SceneSerializer::DeserializeFromSnapshots(const std::string& meta, const std::vector<std::string>& entityBlocks)
	{
		// Reassemble the metadata + the given entity blocks into a full scene document and run the
		// normal whole-scene deserialize — restore never touches entities in place, so it can't
		// corrupt the two-way parent/child links.
		YAML::Node root = meta.empty() ? YAML::Node(YAML::NodeType::Map) : YAML::Load(meta);

		YAML::Node entities(YAML::NodeType::Sequence);
		for (const std::string& block : entityBlocks)
			entities.push_back(YAML::Load(block));
		root["Entities"] = entities;

		YAML::Emitter out;
		out << root;
		return DeserializeFromYAML(std::string(out.c_str()));
	}

	bool SceneSerializer::RunRoundTripSelfTests(std::vector<std::string>* failures)
	{
		bool ok = true;
		auto fail = [&](const std::string& message)
		{
			ok = false;
			if (failures)
				failures->push_back(message);
		};

		// Build a small scene with a two-level hierarchy and a couple of components.
		Ref<Scene> src = Ref<Scene>::Create();
		Entity parent = src->CreateEntity("Parent");
		Entity childA = src->CreateEntity("ChildA");
		Entity childB = src->CreateEntity("ChildB");
		childA.SetParent(parent);
		childB.SetParent(parent);
		parent.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
		childA.AddComponent<PointLightComponent>().Radiance = { 0.5f, 0.25f, 0.1f };
		childB.AddComponent<DirectionalLightComponent>();
		auto& audio = childB.AddComponent<AudioSourceComponent>();
		audio.Event = { "{12345678-1234-1234-1234-123456789abc}", "event:/Test/Door", "Test.bank" };
		audio.ParameterOverrides = { { "Size", 0.75f }, { "Urgency", 2.0f } };
		audio.Config.PlayOnAwake = false;
		audio.LegacyAudio = 456;
		audio.LegacyLooping = true;
		audio.ScriptPaused = true;
		auto& meshCollider = childB.AddComponent<MeshColliderComponent>();
		meshCollider.Acoustic = AcousticMaterial::Wood;
		meshCollider.AcousticMotion = AcousticGeometryMode::Dynamic;
		auto& portal = childB.AddComponent<AudioPortalComponent>();
		portal.Open = 0.35f;
		portal.HalfExtents = { 2.0f, 3.0f, 0.1f };
		portal.BlendDistance = 4.0f;
		portal.Material = AcousticMaterial::Glass;
		portal.Enabled = false;
		const AudioPortalComponent expectedPortal = portal;
		auto& surface = childB.AddComponent<AudioSurfaceComponent>();
		surface.Material = AcousticMaterial::Carpet;
		surface.FootstepOverride = audio.Event;
		surface.ImpactOverride = { "{11111111-1234-1234-1234-123456789abc}", "event:/Impact", "Impact.bank" };
		surface.AutoFootsteps = true;
		surface.PhysicsSounds = false;
		surface.StrideLength = 1.1f;
		surface.GroundProbeDistance = 1.5f;
		surface.FootstepWeight = 90.0f;
		const AudioSurfaceComponent expectedSurface = surface;
		auto& music = childB.AddComponent<MusicDirectorComponent>();
		music.Event = audio.Event;
		music.InitialState = "Combat";
		music.Intensity = 0.75f;
		music.PlayOnAwake = false;
		const MusicDirectorComponent expectedMusic = music;
		auto& zone = childB.AddComponent<AudioZoneComponent>();
		zone.Shape = AudioZoneShape::Sphere;
		zone.Offset = { 1.0f, 2.0f, 3.0f };
		zone.HalfExtents = { 3.0f, 4.0f, 5.0f };
		zone.Radius = 7.0f;
		zone.Priority = 5.0f;
		zone.BlendDistance = 1.5f;
		zone.FadeTime = 0.4f;
		zone.Volume = 0.7f;
		zone.Enabled = false;
		zone.AmbienceEvent = audio.Event;
		zone.Snapshot = { "{87654321-1234-1234-1234-123456789abc}", "snapshot:/Cave", "Test.bank" };
		const AudioZoneComponent expectedZone = zone;
		const AudioSourceComponent expectedAudio = audio;
		auto checkAudioCopy = [&](Entity entity, const char* operation)
		{
			if (!entity || !entity.HasComponent<AudioSourceComponent>())
			{
				fail(std::format("{} lost the audio source", operation));
				return;
			}
			if (!entity.HasComponent<MeshColliderComponent>() || !entity.HasComponent<AudioSurfaceComponent>()
				|| entity.GetComponent<MeshColliderComponent>().Acoustic != AcousticMaterial::Wood
				|| entity.GetComponent<MeshColliderComponent>().AcousticMotion != AcousticGeometryMode::Dynamic
				|| entity.GetComponent<AudioSurfaceComponent>().Material != AcousticMaterial::Carpet)
				fail(std::format("{} lost acoustic material tags", operation));
			if (const auto* copied = entity.TryGetComponent<AudioPortalComponent>())
			{
				if (copied->Enabled != expectedPortal.Enabled || copied->Open != expectedPortal.Open ||
					copied->HalfExtents != expectedPortal.HalfExtents || copied->BlendDistance != expectedPortal.BlendDistance ||
					copied->Material != expectedPortal.Material)
					fail(std::format("{} changed portal data", operation));
			}
			else
				fail(std::format("{} lost the audio portal", operation));
			if (const auto* copied = entity.TryGetComponent<AudioSurfaceComponent>())
			{
				if (copied->FootstepOverride.Guid != expectedSurface.FootstepOverride.Guid
					|| copied->FootstepOverride.Path != expectedSurface.FootstepOverride.Path
					|| copied->FootstepOverride.BankName != expectedSurface.FootstepOverride.BankName
					|| copied->ImpactOverride.Guid != expectedSurface.ImpactOverride.Guid
					|| copied->ImpactOverride.Path != expectedSurface.ImpactOverride.Path
					|| copied->ImpactOverride.BankName != expectedSurface.ImpactOverride.BankName
					|| copied->AutoFootsteps != expectedSurface.AutoFootsteps || copied->PhysicsSounds != expectedSurface.PhysicsSounds
					|| copied->StrideLength != expectedSurface.StrideLength || copied->GroundProbeDistance != expectedSurface.GroundProbeDistance
					|| copied->FootstepWeight != expectedSurface.FootstepWeight)
					fail(std::format("{} changed physics audio settings", operation));
			}
			if (const auto* copiedMusic = entity.TryGetComponent<MusicDirectorComponent>())
			{
				if (copiedMusic->Event.Guid != expectedMusic.Event.Guid || copiedMusic->Event.Path != expectedMusic.Event.Path
					|| copiedMusic->Event.BankName != expectedMusic.Event.BankName || copiedMusic->InitialState != expectedMusic.InitialState
					|| copiedMusic->Intensity != expectedMusic.Intensity || copiedMusic->PlayOnAwake != expectedMusic.PlayOnAwake)
					fail(std::format("{} changed music startup settings", operation));
			}
			else
				fail(std::format("{} lost the music director", operation));
			if (!entity.HasComponent<AudioZoneComponent>())
				fail(std::format("{} lost the audio zone", operation));
			else
			{
				const auto& copiedZone = entity.GetComponent<AudioZoneComponent>();
				if (copiedZone.Enabled != expectedZone.Enabled || copiedZone.Shape != expectedZone.Shape
					|| copiedZone.Offset != expectedZone.Offset || copiedZone.HalfExtents != expectedZone.HalfExtents
					|| copiedZone.Radius != expectedZone.Radius || copiedZone.Priority != expectedZone.Priority
					|| copiedZone.BlendDistance != expectedZone.BlendDistance || copiedZone.FadeTime != expectedZone.FadeTime
					|| copiedZone.Volume != expectedZone.Volume || copiedZone.AmbienceEvent.Guid != expectedZone.AmbienceEvent.Guid
					|| copiedZone.Snapshot.Guid != expectedZone.Snapshot.Guid || copiedZone.Snapshot.Path != expectedZone.Snapshot.Path
					|| copiedZone.Snapshot.BankName != expectedZone.Snapshot.BankName)
					fail(std::format("{} changed audio zone data", operation));
			}
			const auto& copied = entity.GetComponent<AudioSourceComponent>();
			if (copied.Event.Guid != expectedAudio.Event.Guid || copied.ParameterOverrides != expectedAudio.ParameterOverrides
				|| copied.Config.PlayOnAwake || copied.ScriptPaused
				|| copied.LegacyAudio != expectedAudio.LegacyAudio || copied.LegacyLooping != expectedAudio.LegacyLooping)
				fail(std::format("{} changed audio event data or copied runtime playback state", operation));
		};
		Entity duplicate = src->DuplicateEntity(childB);
		checkAudioCopy(duplicate, "Duplicate");
		Ref<Prefab> prefab = Ref<Prefab>::Create();
		prefab->Create(childB, false);
		checkAudioCopy(prefab->GetScene()->TryGetEntityWithUUID(prefab->GetRootEntityID()), "Prefab creation");
		checkAudioCopy(src->Instantiate(prefab), "Prefab instantiation");
		src->ReconcilePrefabComponents(duplicate, parent);
		if (duplicate.HasComponent<AudioPortalComponent>() || duplicate.HasComponent<MusicDirectorComponent>() || duplicate.HasComponent<AudioZoneComponent>() || duplicate.HasComponent<AudioSourceComponent>() || duplicate.HasComponent<AudioSurfaceComponent>() || duplicate.HasComponent<MeshColliderComponent>())
			fail("Prefab reconciliation did not remove an absent audio source");
		src->ReconcilePrefabComponents(duplicate, childB);
		checkAudioCopy(duplicate, "Prefab reconciliation");

		Entity portalRig = src->CreateEntity("PortalRig");
		Entity roomA = src->CreateChildEntity(portalRig, "RoomA");
		Entity roomB = src->CreateChildEntity(portalRig, "RoomB");
		roomA.AddComponent<AudioZoneComponent>();
		roomB.AddComponent<AudioZoneComponent>();
		auto& linked = portalRig.AddComponent<AudioPortalComponent>();
		linked.ZoneA = roomA.GetUUID();
		linked.ZoneB = roomB.GetUUID();
		const auto checkPortal = [&](Entity root, const char* operation)
		{
			const auto* copied = root.TryGetComponent<AudioPortalComponent>();
			if (!copied || root.Children().size() != 2 || copied->ZoneA != root.Children()[0] || copied->ZoneB != root.Children()[1])
				fail(std::format("{} failed to remap portal room references", operation));
		};
		checkPortal(src->DuplicateEntity(portalRig), "Duplicate portal");
		Ref<Prefab> portalPrefab = Ref<Prefab>::Create();
		portalPrefab->Create(portalRig, false);
		Entity prefabPortal = portalPrefab->GetScene()->TryGetEntityWithUUID(portalPrefab->GetRootEntityID());
		checkPortal(prefabPortal, "Create portal prefab");
		Entity portalInstance = src->Instantiate(portalPrefab);
		checkPortal(portalInstance, "Instantiate portal prefab");
		if (GetOverriddenComponentKeys(portalInstance, prefabPortal).contains("AudioPortalComponent"))
			fail("remapped portal room IDs were incorrectly detected as prefab overrides");
		portalInstance.GetComponent<AudioPortalComponent>().ZoneA = 0;
		if (!GetOverriddenComponentKeys(portalInstance, prefabPortal).contains("AudioPortalComponent"))
			fail("cleared portal room was not detected as a prefab override");
		Scene::ReconcilePrefabComponents(portalInstance, prefabPortal);
		checkPortal(portalInstance, "Revert portal prefab");
		Scene::ReconcilePrefabComponents(prefabPortal, portalInstance);
		checkPortal(prefabPortal, "Apply portal prefab");

		Entity listenerRig = src->CreateEntity("ListenerRig");
		Entity attenuationTarget = src->CreateChildEntity(listenerRig, "ListenerTarget");
		auto& listener = listenerRig.AddComponent<AudioListenerComponent>();
		listener.ListenerIndex = 7;
		listener.Weight = 0.25f;
		listener.UseAttenuationTarget = true;
		listener.AttenuationTarget = attenuationTarget.GetUUID();
		const auto checkListener = [&](Entity root, const char* operation)
		{
			if (!root || !root.HasComponent<AudioListenerComponent>() || root.Children().size() != 1)
			{
				fail(std::format("{} lost the listener hierarchy", operation));
				return;
			}
			const auto& copied = root.GetComponent<AudioListenerComponent>();
			if (!copied.Active || copied.ListenerIndex != 7 || copied.Weight != 0.25f || !copied.UseAttenuationTarget
				|| copied.AttenuationTarget != root.Children()[0])
				fail(std::format("{} changed listener data or failed to remap its target", operation));
		};
		checkListener(src->DuplicateEntity(listenerRig), "Duplicate listener");
		Ref<Prefab> listenerPrefab = Ref<Prefab>::Create();
		listenerPrefab->Create(listenerRig, false);
		Entity prefabListener = listenerPrefab->GetScene()->TryGetEntityWithUUID(listenerPrefab->GetRootEntityID());
		checkListener(prefabListener, "Create listener prefab");
		Entity listenerInstance = src->Instantiate(listenerPrefab);
		Entity secondListenerInstance = src->Instantiate(listenerPrefab);
		checkListener(listenerInstance, "Instantiate listener prefab");
		checkListener(secondListenerInstance, "Instantiate second listener prefab");
		Entity nestedListenerInstance = src->InstantiateChild(listenerPrefab, secondListenerInstance);
		Scene::ReconcilePrefabComponents(nestedListenerInstance, prefabListener);
		checkListener(nestedListenerInstance, "Revert nested listener prefab");
		if (GetOverriddenComponentKeys(nestedListenerInstance, prefabListener).contains("AudioListenerComponent"))
			fail("nested listener instance mapped its target into its parent instance");
		if (GetOverriddenComponentKeys(listenerInstance, prefabListener).contains("AudioListenerComponent"))
			fail("remapped listener target was incorrectly marked as a prefab override");
		listenerInstance.GetComponent<AudioListenerComponent>().AttenuationTarget = secondListenerInstance.Children()[0];
		if (!GetOverriddenComponentKeys(listenerInstance, prefabListener).contains("AudioListenerComponent"))
			fail("external listener target override was not detected");
		Scene::ReconcilePrefabComponents(listenerInstance, prefabListener);
		checkListener(listenerInstance, "Revert listener prefab");
		Scene::ReconcilePrefabComponents(prefabListener, listenerInstance);
		checkListener(prefabListener, "Apply listener prefab");
		Scene::ReconcilePrefabComponents(listenerInstance, parent);
		if (listenerInstance.HasComponent<AudioListenerComponent>())
			fail("prefab reconciliation did not remove an absent listener");
		Scene::ReconcilePrefabComponents(listenerInstance, prefabListener);
		checkListener(listenerInstance, "Restore listener prefab");
		listenerRig.GetComponent<AudioListenerComponent>().AttenuationTarget = childB.GetUUID();
		Entity externalCopy = src->DuplicateEntity(listenerRig);
		if (externalCopy.GetComponent<AudioListenerComponent>().AttenuationTarget != childB.GetUUID())
			fail("duplicate lost an external scene listener target");
		Ref<Prefab> externalPrefab = Ref<Prefab>::Create();
		externalPrefab->Create(listenerRig, false);
		if (externalPrefab->GetScene()->TryGetEntityWithUUID(externalPrefab->GetRootEntityID()).GetComponent<AudioListenerComponent>().AttenuationTarget != 0)
			fail("prefab retained a listener target outside its hierarchy");
		listenerRig.GetComponent<AudioListenerComponent>().AttenuationTarget = attenuationTarget.GetUUID();

		std::string meta1;
		std::map<UUID, std::string> ents1 = SceneSerializer(src).SerializeEntitySnapshots(meta1);

		std::vector<std::string> blocks;
		blocks.reserve(ents1.size());
		for (const auto& [handle, block] : ents1)
			blocks.push_back(block);

		Ref<Scene> dst = Ref<Scene>::Create();
		if (!SceneSerializer(dst).DeserializeFromSnapshots(meta1, blocks))
		{
			fail("DeserializeFromSnapshots returned false");
			return ok;
		}

		checkPortal(dst->TryGetEntityWithUUID(portalRig.GetUUID()), "Portal round-trip");
		checkListener(dst->TryGetEntityWithUUID(listenerRig.GetUUID()), "Listener round-trip");
		std::string meta2;
		Entity restoredAudioEntity = dst->TryGetEntityWithUUID(childB.GetUUID());
		if (!restoredAudioEntity || !restoredAudioEntity.HasComponent<AudioSourceComponent>())
			fail("audio source disappeared during the round-trip");
		else
		{
			checkAudioCopy(restoredAudioEntity, "Scene roundtrip");
			const auto& restoredAudio = restoredAudioEntity.GetComponent<AudioSourceComponent>();
			if (restoredAudio.Event.Guid != expectedAudio.Event.Guid || restoredAudio.Event.Path != expectedAudio.Event.Path
				|| restoredAudio.Event.BankName != expectedAudio.Event.BankName || restoredAudio.ParameterOverrides != expectedAudio.ParameterOverrides
				|| restoredAudio.Config.PlayOnAwake || restoredAudio.ScriptPaused
				|| restoredAudio.LegacyAudio != expectedAudio.LegacyAudio || restoredAudio.LegacyLooping != expectedAudio.LegacyLooping)
				fail("audio event reference/overrides changed or runtime playback state was serialized");
		}
		std::map<UUID, std::string> ents2 = SceneSerializer(dst).SerializeEntitySnapshots(meta2);

		if (meta1 != meta2)
			fail("scene metadata was not reproduced by the round-trip");
		if (ents1.size() != ents2.size())
			fail(std::format("entity count changed by the round-trip ({} -> {})", ents1.size(), ents2.size()));

		for (const auto& [handle, block] : ents1)
		{
			auto it = ents2.find(handle);
			if (it == ents2.end())
				fail(std::format("entity {} missing after round-trip", (uint64_t)handle));
			else if (it->second != block)
				fail(std::format("entity {} YAML differs after round-trip", (uint64_t)handle));
		}

		return ok;
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
		YAML::Node data;
		try
		{
			data = YAML::Load(yamlString);
		}
		catch (const YAML::Exception& e)
		{
			LUX_CORE_ERROR("Failed to parse scene YAML: {0}", e.what());
			return false;
		}

		if (!data || !data.IsMap() || !data["Scene"])
		{
			LUX_CORE_ERROR("Scene YAML is missing the required Scene root field.");
			return false;
		}

		if (data["Entities"] && !data["Entities"].IsSequence())
		{
			LUX_CORE_ERROR("Scene YAML has an invalid Entities field; expected a sequence.");
			return false;
		}

		m_Scene->m_Registry.clear();
		m_Scene->m_EntityMap.clear();

		try
		{
			m_Scene->SetName(data["Scene"].as<std::string>());
		}
		catch (const YAML::Exception& e)
		{
			LUX_CORE_ERROR("Failed to read scene name: {0}", e.what());
			return false;
		}

		// Absent in scenes written before post-processing moved off volumes; those simply
		// keep the struct defaults.
		if (auto postProcess = data["PostProcess"])
		{
			PostProcessSettings post;
			post.Exposure = postProcess["Exposure"].as<float>(post.Exposure);
			post.ExposureControl = static_cast<ExposureMode>(postProcess["ExposureMode"].as<uint32_t>(static_cast<uint32_t>(post.ExposureControl)));
			post.Aperture = postProcess["Aperture"].as<float>(post.Aperture);
			post.ShutterSpeed = postProcess["ShutterSpeed"].as<float>(post.ShutterSpeed);
			post.ISO = postProcess["ISO"].as<float>(post.ISO);
			post.ExposureEV100 = postProcess["ExposureEV100"].as<float>(post.ExposureEV100);
			post.ExposureCompensation = postProcess["ExposureCompensation"].as<float>(post.ExposureCompensation);
			post.AutoMinEV100 = postProcess["AutoMinEV100"].as<float>(post.AutoMinEV100);
			post.AutoMaxEV100 = postProcess["AutoMaxEV100"].as<float>(post.AutoMaxEV100);
			post.AutoAdaptationSpeedUp = postProcess["AutoAdaptationSpeedUp"].as<float>(post.AutoAdaptationSpeedUp);
			post.AutoAdaptationSpeedDown = postProcess["AutoAdaptationSpeedDown"].as<float>(post.AutoAdaptationSpeedDown);
			post.ColorFilter = postProcess["ColorFilter"].as<glm::vec3>(post.ColorFilter);
			post.Saturation = postProcess["Saturation"].as<float>(post.Saturation);
			post.Contrast = postProcess["Contrast"].as<float>(post.Contrast);
			post.Gamma = postProcess["Gamma"].as<float>(post.Gamma);
			post.Tonemap = static_cast<TonemapOperator>(postProcess["Tonemap"].as<uint32_t>(static_cast<uint32_t>(post.Tonemap)));
			post.WhiteTemperature = postProcess["WhiteTemperature"].as<float>(post.WhiteTemperature);
			post.WhiteTint = postProcess["WhiteTint"].as<float>(post.WhiteTint);
			post.Lift = postProcess["Lift"].as<glm::vec3>(post.Lift);
			post.GradeGamma = postProcess["GradeGamma"].as<glm::vec3>(post.GradeGamma);
			post.Gain = postProcess["Gain"].as<glm::vec3>(post.Gain);
			m_Scene->SetPostProcessSettings(post);
		}

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
		catch (const std::exception& e)
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
