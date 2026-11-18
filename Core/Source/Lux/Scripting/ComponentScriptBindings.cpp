// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ComponentScriptBindings.h"
#include "ScriptEngine.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Application.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

#include <Coral/Assembly.hpp>
#include <Coral/String.hpp>

#include <cmath>
#include <unordered_map>

namespace Lux
{
	namespace
	{
		// The values of the C# enums of the same names, in the same order.
		enum class LightKind : int32_t { Directional = 0, Point = 1, Spot = 2, Area = 3, Sky = 4 };
		enum class LightField : int32_t
		{
			Intensity = 0, Range, Angle, ColorTemperature, ShadowAmount, Width, Height, Radius, Length, Rotation,
			CastShadows = 10, SoftShadows, UseColorTemperature, TwoSided
		};
		enum class CameraField : int32_t { FieldOfView = 0, NearClip, FarClip, Orthographic, OrthographicSize };
		enum class MaterialValue : int32_t { Color = 0, Metalness, Roughness, Emission, Transparency };
		enum class MusicField : int32_t { PlayOnAwake = 0, Intensity };

		// The material copies scripts made for single entities, and the entity each belongs to: a copy is
		// edited in place only by its own entity, so two entities never share one by accident.
		std::unordered_map<AssetHandle, UUID> s_PrivateMaterials;

		Scene* CurrentScene()
		{
			if (!Application::IsMainThread())
			{
				LUX_CORE_ERROR_TAG("ScriptEngine", "Component scripting calls must run on the main thread");
				return nullptr;
			}
			return ScriptEngine::GetInstance().GetCurrentScene().Raw();
		}

		// A script can keep an Entity after it was destroyed, or ask for a component the entity lacks: say so, once per call,
		// instead of acting on nothing in silence.
		Entity EntityOf(uint64_t id)
		{
			Scene* scene = CurrentScene();
			if (!scene)
				return {};
			Entity entity = scene->TryGetEntityWithUUID(id);
			if (!entity)
				LUX_CONSOLE_LOG_ERROR("A script used entity {} after it was destroyed (or it never existed)", id);
			return entity;
		}

		template<typename TComponent>
		TComponent* ComponentOf(uint64_t id)
		{
			Entity entity = EntityOf(id);
			if (!entity)
				return nullptr;
			TComponent* component = entity.TryGetComponent<TComponent>();
			if (!component)
				LUX_CONSOLE_LOG_ERROR("Entity {} ({}) does not have that component", id, entity.GetName());
			return component;
		}

		bool IsFinite(float value)
		{
			if (std::isfinite(value))
				return true;
			LUX_CONSOLE_LOG_ERROR("A component value must be a finite number, not {}", value);
			return false;
		}

		// -- Lights -----------------------------------------------------------------------------------

		// Light fields are floats or bools stored in one of five components; the pointer is null when this
		// kind of light has no such field.
		template<typename TLight>
		float* FloatFieldOf(TLight& light, LightField field)
		{
			switch (field)
			{
				case LightField::Intensity: return &light.Intensity;
				case LightField::ColorTemperature: return &light.ColorTemperature;
				default: break;
			}
			if constexpr (requires { light.Range; })
			{
				if (field == LightField::Range)
					return &light.Range;
			}
			else if constexpr (requires { light.Radius; })
			{
				// A point light's Radius is its range; spot and area lights have a Range of their own.
				if (field == LightField::Range)
					return &light.Radius;
			}
			if constexpr (requires { light.Angle; })
			{
				if (field == LightField::Angle)
					return &light.Angle;
			}
			if constexpr (requires { light.ShadowAmount; })
			{
				if (field == LightField::ShadowAmount)
					return &light.ShadowAmount;
			}
			if constexpr (requires { light.Width; light.Height; light.Length; })
			{
				if (field == LightField::Width) return &light.Width;
				if (field == LightField::Height) return &light.Height;
				if (field == LightField::Length) return &light.Length;
			}
			if constexpr (requires { light.Radius; })
			{
				if (field == LightField::Radius)
					return &light.Radius;
			}
			return nullptr;
		}

		template<typename TLight>
		bool* BoolFieldOf(TLight& light, LightField field)
		{
			if (field == LightField::UseColorTemperature)
				return &light.UseColorTemperature;
			if constexpr (requires { light.CastShadows; })
			{
				if (field == LightField::CastShadows)
					return &light.CastShadows;
			}
			if constexpr (requires { light.CastsShadows; })
			{
				if (field == LightField::CastShadows)
					return &light.CastsShadows;
			}
			if (field == LightField::SoftShadows)
				return &light.SoftShadows;
			if constexpr (requires { light.TwoSided; })
			{
				if (field == LightField::TwoSided)
					return &light.TwoSided;
			}
			return nullptr;
		}

		// Runs `use(light)` on the light component of the given kind, whichever struct that is.
		template<typename Use>
		bool WithLight(uint64_t id, LightKind kind, Use&& use)
		{
			switch (kind)
			{
				case LightKind::Directional: if (auto* c = ComponentOf<DirectionalLightComponent>(id)) return use(*c); break;
				case LightKind::Point: if (auto* c = ComponentOf<PointLightComponent>(id)) return use(*c); break;
				case LightKind::Spot: if (auto* c = ComponentOf<SpotLightComponent>(id)) return use(*c); break;
				case LightKind::Area: if (auto* c = ComponentOf<AreaLightComponent>(id)) return use(*c); break;
				default: break;
			}
			return false;
		}

		float Light_GetScalar(uint64_t id, int32_t kind, int32_t field)
		{
			if (static_cast<LightKind>(kind) == LightKind::Sky)
			{
				const auto* sky = ComponentOf<SkyLightComponent>(id);
				if (!sky)
					return 0.0f;
				switch (static_cast<LightField>(field))
				{
					case LightField::Intensity: return sky->Intensity;
					case LightField::Rotation: return sky->Rotation;
					default: return 0.0f;
				}
			}

			float result = 0.0f;
			WithLight(id, static_cast<LightKind>(kind), [&](auto& light)
			{
				if (float* value = FloatFieldOf(light, static_cast<LightField>(field)))
					result = *value;
				else if (bool* flag = BoolFieldOf(light, static_cast<LightField>(field)))
					result = *flag ? 1.0f : 0.0f;
				return true;
			});
			return result;
		}

		Coral::Bool32 Light_SetScalar(uint64_t id, int32_t kind, int32_t field, float value)
		{
			if (!IsFinite(value))
				return false;

			if (static_cast<LightKind>(kind) == LightKind::Sky)
			{
				auto* sky = ComponentOf<SkyLightComponent>(id);
				if (!sky)
					return false;
				switch (static_cast<LightField>(field))
				{
					case LightField::Intensity: sky->Intensity = std::max(value, 0.0f); return true;
					case LightField::Rotation: sky->Rotation = value; return true;
					default: return false;
				}
			}

			return WithLight(id, static_cast<LightKind>(kind), [&](auto& light)
			{
				if (float* target = FloatFieldOf(light, static_cast<LightField>(field)))
				{
					// Every float a script can set is a size, a range or an intensity: none is negative.
					*target = std::max(value, 0.0f);
					return true;
				}
				if (bool* flag = BoolFieldOf(light, static_cast<LightField>(field)))
				{
					*flag = value != 0.0f;
					return true;
				}
				return false;
			});
		}

		Coral::Bool32 Light_GetColor(uint64_t id, int32_t kind, glm::vec3* out)
		{
			return WithLight(id, static_cast<LightKind>(kind), [&](auto& light) { *out = light.Radiance; return true; });
		}

		Coral::Bool32 Light_SetColor(uint64_t id, int32_t kind, glm::vec3* in)
		{
			if (!IsFinite(in->x) || !IsFinite(in->y) || !IsFinite(in->z))
				return false;
			return WithLight(id, static_cast<LightKind>(kind), [&](auto& light) { light.Radiance = glm::max(*in, glm::vec3(0.0f)); return true; });
		}

		// -- Camera -----------------------------------------------------------------------------------

		float Camera_GetScalar(uint64_t id, int32_t field)
		{
			const auto* component = ComponentOf<CameraComponent>(id);
			if (!component)
				return 0.0f;
			const SceneCamera& camera = component->Camera;
			const bool ortho = camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic;
			switch (static_cast<CameraField>(field))
			{
				case CameraField::FieldOfView: return camera.GetDegPerspectiveVerticalFOV();
				case CameraField::NearClip: return ortho ? camera.GetOrthographicNearClip() : camera.GetPerspectiveNearClip();
				case CameraField::FarClip: return ortho ? camera.GetOrthographicFarClip() : camera.GetPerspectiveFarClip();
				case CameraField::Orthographic: return ortho ? 1.0f : 0.0f;
				case CameraField::OrthographicSize: return camera.GetOrthographicSize();
			}
			return 0.0f;
		}

		Coral::Bool32 Camera_SetScalar(uint64_t id, int32_t field, float value)
		{
			auto* component = ComponentOf<CameraComponent>(id);
			if (!component || !IsFinite(value))
				return false;
			SceneCamera& camera = component->Camera;
			const bool ortho = camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic;
			switch (static_cast<CameraField>(field))
			{
				case CameraField::FieldOfView:
					if (value <= 0.0f || value >= 180.0f)
						return false;
					camera.SetDegPerspectiveVerticalFOV(value);
					break;
				case CameraField::NearClip:
					if (ortho)
						camera.SetOrthographicNearClip(value);
					else if (value > 0.0f)
						camera.SetPerspectiveNearClip(value);
					else
						return false;
					break;
				case CameraField::FarClip:
					if (ortho)
						camera.SetOrthographicFarClip(value);
					else if (value > camera.GetPerspectiveNearClip())
						camera.SetPerspectiveFarClip(value);
					else
						return false;
					break;
				case CameraField::Orthographic:
				{
					const auto type = value != 0.0f ? SceneCamera::ProjectionType::Orthographic : SceneCamera::ProjectionType::Perspective;
					camera.SetProjectionType(type);
					component->ProjectionType = static_cast<CameraComponent::Type>(type);
					break;
				}
				case CameraField::OrthographicSize:
					if (value <= 0.0f)
						return false;
					camera.SetOrthographicSize(value);
					break;
				default:
					return false;
			}

			// The projection matrix is rebuilt from the viewport, so ask it to look at the new numbers.
			const auto [left, top, right, bottom] = camera.GetViewportBounds();
			camera.SetViewportBounds(left, top, right, bottom);
			return true;
		}

		// -- Static mesh and its materials ------------------------------------------------------------

		bool ResolveAssets(const StaticMeshComponent& component, Ref<StaticMesh>& staticMesh, Ref<MeshSource>& meshSource)
		{
			staticMesh = StaticMesh::GetOrCreateRuntime(component.StaticMesh);
			if (!staticMesh)
				return false;
			meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource());
			return meshSource != nullptr;
		}

		uint32_t MaterialSlotCount(const StaticMeshComponent& component)
		{
			Ref<StaticMesh> staticMesh;
			Ref<MeshSource> meshSource;
			return ResolveAssets(component, staticMesh, meshSource) ? static_cast<uint32_t>(meshSource->GetMaterials().size()) : 0u;
		}

		AssetHandle SlotMaterialHandle(const StaticMeshComponent& component, uint32_t slot)
		{
			Ref<StaticMesh> staticMesh;
			Ref<MeshSource> meshSource;
			if (!ResolveAssets(component, staticMesh, meshSource) || slot >= meshSource->GetMaterials().size())
				return 0;
			return ResolveStaticMeshMaterialHandle(component.MaterialTable, staticMesh, meshSource, slot);
		}

		Ref<MaterialAsset> SlotMaterial(const StaticMeshComponent& component, uint32_t slot)
		{
			const AssetHandle handle = SlotMaterialHandle(component, slot);
			if (!handle || !AssetManager::IsAssetHandleValid(handle) || AssetManager::GetAssetType(handle) != AssetType::Material)
				return nullptr;
			return AssetManager::GetAsset<MaterialAsset>(handle);
		}

		// A table with a lone entry at slot 0 stands for every slot (ResolveStaticMeshMaterialHandle), so before one slot's
		// material changes, every slot's current material is written down. All are resolved first: writing them one by one
		// would let the first entry answer for the rest.
		void PinMaterialSlots(StaticMeshComponent& component)
		{
			std::vector<std::pair<uint32_t, AssetHandle>> pending;
			for (uint32_t i = 0; i < MaterialSlotCount(component); i++)
			{
				if (!component.MaterialTable->HasMaterial(i))
				{
					if (const AssetHandle existing = SlotMaterialHandle(component, i))
						pending.emplace_back(i, existing);
				}
			}
			for (const auto& [slot, handle] : pending)
				component.MaterialTable->SetMaterial(slot, handle);
		}

		// The entity's own copy of a slot's material, made on the first write: changing a shared material
		// would change every entity that draws with it. The copy takes every value of the original (an
		// instance without a parent link, so nothing flows into it later).
		Ref<MaterialAsset> PrivateSlotMaterial(Entity entity, StaticMeshComponent& component, uint32_t slot)
		{
			const AssetHandle current = SlotMaterialHandle(component, slot);
			if (const auto owner = s_PrivateMaterials.find(current); owner != s_PrivateMaterials.end() && owner->second == entity.GetUUID())
				return AssetManager::GetAsset<MaterialAsset>(current);

			Ref<MaterialAsset> source = SlotMaterial(component, slot);
			if (!source)
				return nullptr;

			PinMaterialSlots(component);

			Ref<MaterialAsset> copy = Ref<MaterialAsset>::Create(source->IsTransparent());
			copy->InheritFrom(*source);
			copy->SetAcousticTag(source->GetAcousticTag());
			const AssetHandle handle = AssetManager::AddMemoryOnlyAsset(copy);
			s_PrivateMaterials[handle] = entity.GetUUID();
			component.MaterialTable->SetMaterial(slot, handle);
			return copy;
		}

		uint64_t StaticMesh_GetMesh(uint64_t id)
		{
			const auto* component = ComponentOf<StaticMeshComponent>(id);
			return component ? static_cast<uint64_t>(component->StaticMesh) : 0;
		}

		Coral::Bool32 StaticMesh_SetMesh(uint64_t id, uint64_t handle)
		{
			auto* component = ComponentOf<StaticMeshComponent>(id);
			if (!component)
				return false;
			if (handle != 0)
			{
				if (!AssetManager::IsAssetHandleValid(handle))
					return false;
				const AssetType type = AssetManager::GetAssetType(handle);
				if (type != AssetType::StaticMesh && type != AssetType::MeshSource)
					return false;
			}
			component->StaticMesh = handle;
			return true;
		}

		int32_t StaticMesh_GetMaterialCount(uint64_t id)
		{
			const auto* component = ComponentOf<StaticMeshComponent>(id);
			return component ? static_cast<int32_t>(MaterialSlotCount(*component)) : 0;
		}

		uint64_t StaticMesh_GetMaterial(uint64_t id, int32_t slot)
		{
			const auto* component = ComponentOf<StaticMeshComponent>(id);
			if (!component || slot < 0)
				return 0;
			const AssetHandle handle = SlotMaterialHandle(*component, static_cast<uint32_t>(slot));
			return handle && AssetManager::GetAssetType(handle) == AssetType::Material ? static_cast<uint64_t>(handle) : 0;
		}

		// Assigns a material asset to a slot, for this entity only. 0 goes back to the mesh's own material.
		Coral::Bool32 StaticMesh_SetMaterial(uint64_t id, int32_t slot, uint64_t handle)
		{
			auto* component = ComponentOf<StaticMeshComponent>(id);
			if (!component || slot < 0 || static_cast<uint32_t>(slot) >= MaterialSlotCount(*component))
				return false;
			if (handle == 0)
			{
				component->MaterialTable->ClearMaterial(static_cast<uint32_t>(slot));
				return true;
			}
			if (!AssetManager::IsAssetHandleValid(handle) || AssetManager::GetAssetType(handle) != AssetType::Material)
				return false;

			PinMaterialSlots(*component);
			component->MaterialTable->SetMaterial(static_cast<uint32_t>(slot), handle);
			return true;
		}

		Coral::Bool32 StaticMesh_GetMaterialValue(uint64_t id, int32_t slot, int32_t which, glm::vec4* out)
		{
			const auto* component = ComponentOf<StaticMeshComponent>(id);
			if (!component || slot < 0)
				return false;
			const Ref<MaterialAsset> material = SlotMaterial(*component, static_cast<uint32_t>(slot));
			if (!material)
				return false;
			switch (static_cast<MaterialValue>(which))
			{
				case MaterialValue::Color: *out = glm::vec4(material->GetAlbedoColor(), 1.0f); return true;
				case MaterialValue::Metalness: *out = glm::vec4(material->GetMetalness()); return true;
				case MaterialValue::Roughness: *out = glm::vec4(material->GetRoughness()); return true;
				case MaterialValue::Emission: *out = glm::vec4(material->GetEmission()); return true;
				case MaterialValue::Transparency: *out = glm::vec4(material->GetTransparency()); return true;
			}
			return false;
		}

		Coral::Bool32 StaticMesh_SetMaterialValue(uint64_t id, int32_t slot, int32_t which, glm::vec4* in)
		{
			Entity entity = EntityOf(id);
			auto* component = entity ? entity.TryGetComponent<StaticMeshComponent>() : nullptr;
			if (!component || slot < 0 || static_cast<uint32_t>(slot) >= MaterialSlotCount(*component))
				return false;
			if (!IsFinite(in->x) || !IsFinite(in->y) || !IsFinite(in->z) || !IsFinite(in->w))
				return false;
			Ref<MaterialAsset> material = PrivateSlotMaterial(entity, *component, static_cast<uint32_t>(slot));
			if (!material)
				return false;
			switch (static_cast<MaterialValue>(which))
			{
				case MaterialValue::Color: material->SetAlbedoColor(glm::max(glm::vec3(*in), glm::vec3(0.0f))); return true;
				case MaterialValue::Metalness: material->SetMetalness(glm::clamp(in->x, 0.0f, 1.0f)); return true;
				case MaterialValue::Roughness: material->SetRoughness(glm::clamp(in->x, 0.0f, 1.0f)); return true;
				case MaterialValue::Emission: material->SetEmission(std::max(in->x, 0.0f)); return true;
				case MaterialValue::Transparency: material->SetTransparency(glm::clamp(in->x, 0.0f, 1.0f)); return true;
			}
			return false;
		}

		// -- Music director -----------------------------------------------------------------------------

		float MusicDirector_GetScalar(uint64_t id, int32_t field)
		{
			const auto* component = ComponentOf<MusicDirectorComponent>(id);
			if (!component)
				return 0.0f;
			return static_cast<MusicField>(field) == MusicField::PlayOnAwake ? (component->PlayOnAwake ? 1.0f : 0.0f) : component->Intensity;
		}

		Coral::Bool32 MusicDirector_SetScalar(uint64_t id, int32_t field, float value)
		{
			auto* component = ComponentOf<MusicDirectorComponent>(id);
			if (!component || !IsFinite(value))
				return false;
			if (static_cast<MusicField>(field) == MusicField::PlayOnAwake)
				component->PlayOnAwake = value != 0.0f;
			else
				component->Intensity = glm::clamp(value, 0.0f, 1.0f);
			return true;
		}

		Coral::String MusicDirector_GetInitialState(uint64_t id)
		{
			const auto* component = ComponentOf<MusicDirectorComponent>(id);
			return Coral::String::New(component ? component->InitialState : std::string());
		}

		Coral::Bool32 MusicDirector_SetInitialState(uint64_t id, Coral::String state)
		{
			auto* component = ComponentOf<MusicDirectorComponent>(id);
			if (!component)
				return false;
			component->InitialState = state;
			return true;
		}
	}

	void ComponentScriptBindings::Register(Coral::ManagedAssembly& assembly)
	{
		s_PrivateMaterials.clear();

#define LUX_ADD_COMPONENT_CALL(Name) assembly.AddInternalCall("Lux.InternalCalls", #Name, reinterpret_cast<void*>(&Name))
		LUX_ADD_COMPONENT_CALL(Light_GetScalar);
		LUX_ADD_COMPONENT_CALL(Light_SetScalar);
		LUX_ADD_COMPONENT_CALL(Light_GetColor);
		LUX_ADD_COMPONENT_CALL(Light_SetColor);
		LUX_ADD_COMPONENT_CALL(Camera_GetScalar);
		LUX_ADD_COMPONENT_CALL(Camera_SetScalar);
		LUX_ADD_COMPONENT_CALL(StaticMesh_GetMesh);
		LUX_ADD_COMPONENT_CALL(StaticMesh_SetMesh);
		LUX_ADD_COMPONENT_CALL(StaticMesh_GetMaterialCount);
		LUX_ADD_COMPONENT_CALL(StaticMesh_GetMaterial);
		LUX_ADD_COMPONENT_CALL(StaticMesh_SetMaterial);
		LUX_ADD_COMPONENT_CALL(StaticMesh_GetMaterialValue);
		LUX_ADD_COMPONENT_CALL(StaticMesh_SetMaterialValue);
		LUX_ADD_COMPONENT_CALL(MusicDirector_GetScalar);
		LUX_ADD_COMPONENT_CALL(MusicDirector_SetScalar);
		LUX_ADD_COMPONENT_CALL(MusicDirector_GetInitialState);
		LUX_ADD_COMPONENT_CALL(MusicDirector_SetInitialState);
#undef LUX_ADD_COMPONENT_CALL
	}
}
