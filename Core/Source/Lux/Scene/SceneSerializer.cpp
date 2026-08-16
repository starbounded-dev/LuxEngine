#include "lpch.h"
#include "SceneSerializer.h"

#include "Components.h"
#include "Entity.h"

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

			return entity["AudioData"] || entity["AudioSourceComponent"] || entity["AudioListenerComponent"]
				|| entity["AnimationComponent"];
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
				out << YAML::Key << "CollisionComplexity" << YAML::Value << (uint8_t)collider.CollisionComplexity;
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
					component.Material.Density = meshCollider["Density"].as<float>(1.0f);
					component.Material.Friction = meshCollider["Friction"].as<float>(0.5f);
					component.Material.Restitution = meshCollider["Restitution"].as<float>(0.0f);
					component.CollisionComplexity = (ECollisionComplexity)meshCollider["CollisionComplexity"].as<uint8_t>((uint8_t)ECollisionComplexity::Default);
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
