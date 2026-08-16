#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include "SceneCamera.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Audio/AudioListener.h"
#include "Lux/Audio/AudioSource.h"
#include "Lux/Core/UUID.h"
#include "Lux/Math/Math.h"
#include "Lux/Physics/PhysicsTypes.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/UI/Font.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Lux {

	struct AudioData // For audio sources only!
	{
		std::vector<AssetHandle> Playlist;
		bool UsePlaylist = false;
		bool RepeatPlaylist = false;
		bool RepeatAfterSpecificTrackPlays = false;
		bool PlayingCurrentIndex = false;
		uint32_t NumberOfAudioSources = 0;
		uint32_t OldIndex = 0;
		uint32_t CurrentIndex = 0;
		uint32_t StartIndex = 0;

		// For Scene:
		bool HasPlayedAudioSource = false;

		// Copies
		std::vector<AssetHandle> PlaylistCopy;
	};

	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
		IDComponent(const UUID& id)
			: ID(id) {}
	};

	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& tag)
			: Tag(tag) {
		}

		operator std::string& () { return Tag; }
		operator const std::string& () const { return Tag; }
	};

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

	private:
		glm::vec3 RotationEuler = { 0.0f, 0.0f, 0.0f };
		glm::quat Rotation = { 1.0f, 0.0f, 0.0f, 0.0f };

	public:
		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& translation)
			: Translation(translation) {
		}

		glm::mat4 GetTransform() const
		{
			return glm::translate(glm::mat4(1.0f), Translation)
				* glm::toMat4(Rotation)
				* glm::scale(glm::mat4(1.0f), Scale);
		}

		void SetTransform(const glm::mat4& transform)
		{
			Math::DecomposeTransform(transform, Translation, Rotation, Scale);
			RotationEuler = glm::eulerAngles(Rotation);
		}

		glm::vec3 GetRotationEuler() const { return RotationEuler; }

		void SetRotationEuler(const glm::vec3& euler)
		{
			RotationEuler = euler;
			Rotation = glm::quat(RotationEuler);
		}

		glm::quat GetRotation() const { return Rotation; }

		void SetRotation(const glm::quat& quat)
		{
			auto wrapToPi = [](glm::vec3 value)
			{
				return glm::mod(value + glm::pi<float>(), 2.0f * glm::pi<float>()) - glm::pi<float>();
			};

			glm::vec3 originalEuler = RotationEuler;
			Rotation = quat;
			RotationEuler = glm::eulerAngles(Rotation);

			const glm::vec3 alternate1 = { RotationEuler.x - glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z - glm::pi<float>() };
			const glm::vec3 alternate2 = { RotationEuler.x + glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z - glm::pi<float>() };
			const glm::vec3 alternate3 = { RotationEuler.x + glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z + glm::pi<float>() };
			const glm::vec3 alternate4 = { RotationEuler.x - glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z + glm::pi<float>() };

			float best = glm::length2(wrapToPi(RotationEuler - originalEuler));
			for (const glm::vec3& candidate : { alternate1, alternate2, alternate3, alternate4 })
			{
				const float distance = glm::length2(wrapToPi(candidate - originalEuler));
				if (distance < best)
				{
					best = distance;
					RotationEuler = candidate;
				}
			}

			RotationEuler = wrapToPi(RotationEuler);
		}

		friend class SceneSerializer;
	};

	struct RelationshipComponent
	{
		UUID ParentHandle = 0;
		std::vector<UUID> Children;

		RelationshipComponent() = default;
		RelationshipComponent(const RelationshipComponent&) = default;
	};

	struct SpriteRendererComponent
	{
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		AssetHandle Texture = 0;
		float TilingFactor = 1.0f;
		glm::vec2 UVStart = { 0.0f, 0.0f };
		glm::vec2 UVEnd = { 1.0f, 1.0f };
		bool ScreenSpace = false;

		SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent&) = default;
		SpriteRendererComponent(const glm::vec4& color)
			: Color(color) {
		}
	};

	struct CircleRendererComponent
	{
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float Thickness = 1.0f;
		float Fade = 0.005f;

		CircleRendererComponent() = default;
		CircleRendererComponent(const CircleRendererComponent&) = default;
	};

	struct CameraComponent
	{
		enum class Type { None = -1, Perspective, Orthographic };

		Type ProjectionType = Type::Perspective;
		SceneCamera Camera;
		bool Primary = true; // TODO: think about moving to Scene
		bool FixedAspectRatio = false;

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;

		operator SceneCamera& () { return Camera; }
		operator const SceneCamera& () const { return Camera; }
	};

	// Forward declaration
	class ScriptableEntity;

	struct ScriptComponent
	{
		std::string ClassName;

		// FNV hash of the full class name; the runtime script identity that keys ScriptEngine's
		// metadata and the Scene's ScriptStorage. Kept in sync with ClassName by the inspector
		// and serializer (Hash::GenerateFNVHash(ClassName)).
		UUID ScriptID = 0;

		// Legacy native-scripting instance (ScriptableEntity path); unused by managed scripting.
		Ref<ScriptableEntity> Instance;

		ScriptComponent();
		ScriptComponent(const ScriptComponent&);
		ScriptComponent& operator=(const ScriptComponent&);
		~ScriptComponent();
	};

	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		ScriptableEntity* (*InstantiateScript)() = nullptr;
		void (*DestroyScript)(NativeScriptComponent*) = nullptr;

		template<typename T>
		void Bind()
		{
			InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
			DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };
		}
	};

	// Physics

	struct RigidBody2DComponent
	{
		enum class Type
		{
			None = -1, Static, Dynamic, Kinematic
		};

		Type BodyType = Type::Static;
		bool FixedRotation = false;
		float Mass = 1.0f;
		float LinearDrag = 0.01f;
		float AngularDrag = 0.05f;
		float GravityScale = 1.0f;
		bool IsBullet = false;

		// Storage for runtime
		void* RuntimeBody = nullptr;

		RigidBody2DComponent() = default;
		RigidBody2DComponent(const RigidBody2DComponent&) = default;
	};

	struct BoxCollider2DComponent
	{
		glm::vec2 Offset = { 0.0f, 0.0f };
		glm::vec2 Size = { 0.5f, 0.5f };

		// TODO: move into physics material (maybe)
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		BoxCollider2DComponent() = default;
		BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
	};

	struct CircleCollider2DComponent
	{
		glm::vec2 Offset = { 0.0f, 0.0f };
		float Radius = 0.5f;

		//TODO: move into physics material (maybe)
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		CircleCollider2DComponent() = default;
		CircleCollider2DComponent(const CircleCollider2DComponent&) = default;
	};

	struct RigidBodyComponent
	{
		EBodyType BodyType = EBodyType::Static;
		uint32_t LayerID = 0;
		bool EnableDynamicTypeChange = false;

		float Mass = 1.0f;
		float LinearDrag = 0.01f;
		float AngularDrag = 0.05f;
		bool DisableGravity = false;
		bool IsTrigger = false;
		ECollisionDetectionType CollisionDetection = ECollisionDetectionType::Discrete;

		glm::vec3 InitialLinearVelocity = glm::vec3(0.0f);
		glm::vec3 InitialAngularVelocity = glm::vec3(0.0f);

		float MaxLinearVelocity = 500.0f;
		float MaxAngularVelocity = 50.0f;

		EActorAxis LockedAxes = EActorAxis::None;

		// Storage for runtime
		void* RuntimeBody = nullptr;

		RigidBodyComponent() = default;
		RigidBodyComponent(const RigidBodyComponent&) = default;
	};

	struct CharacterControllerComponent
	{
		float SlopeLimitDeg = 45.0f;
		float StepOffset = 0.5f;
		uint32_t LayerID = 0;
		bool DisableGravity = false;
		bool ControlMovementInAir = false;
		bool ControlRotationInAir = false;

		CharacterControllerComponent() = default;
		CharacterControllerComponent(const CharacterControllerComponent&) = default;
	};

	struct CompoundColliderComponent
	{
		bool IncludeStaticChildColliders = true;
		bool IsImmutable = true;
		std::vector<UUID> CompoundedColliderEntities;

		CompoundColliderComponent() = default;
		CompoundColliderComponent(const CompoundColliderComponent&) = default;
	};

	struct BoxColliderComponent
	{
		glm::vec3 HalfSize = { 0.5f, 0.5f, 0.5f };
		glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
		ColliderMaterial Material;

		BoxColliderComponent() = default;
		BoxColliderComponent(const BoxColliderComponent&) = default;
	};

	struct SphereColliderComponent
	{
		float Radius = 0.5f;
		glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
		ColliderMaterial Material;

		SphereColliderComponent() = default;
		SphereColliderComponent(const SphereColliderComponent&) = default;
	};

	struct CapsuleColliderComponent
	{
		float Radius = 0.5f;
		float HalfHeight = 0.5f;
		glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
		ColliderMaterial Material;

		CapsuleColliderComponent() = default;
		CapsuleColliderComponent(const CapsuleColliderComponent&) = default;
	};

	struct MeshColliderComponent
	{
		AssetHandle ColliderAsset = 0;
		uint32_t SubmeshIndex = 0;
		bool UseSharedShape = false;
		ColliderMaterial Material;
		ECollisionComplexity CollisionComplexity = ECollisionComplexity::Default;

		MeshColliderComponent() = default;
		MeshColliderComponent(const MeshColliderComponent&) = default;
	};

	struct TextComponent
	{
		std::string TextString = "";
		size_t TextHash = 0;

		// Font
		AssetHandle FontHandle = 0;
		glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float LineSpacing = 0.0f;
		float Kerning = 0.0f;

		// Layout
		float MaxWidth = 10.0f;

		bool ScreenSpace = false;
		bool DropShadow = false;
		float ShadowDistance = 0.0f;
		glm::vec4 ShadowColor = { 0.0f, 0.0f, 0.0f, 1.0f };

		TextComponent() = default;
		TextComponent(const TextComponent& other) = default;
	};

	struct AudioSourceComponent
	{
		AudioSourceConfig Config;

		AssetHandle Audio = 0;
		AudioData AudioSourceData;

		bool Paused = false;
		bool Seek = false;
		uint64_t SeekPosition = 0;

		AssetHandle GetAudioSourceHandle(uint32_t index) const { return AudioSourceData.Playlist[index]; }
		void SetAudioSource(uint32_t index) { Audio = AudioSourceData.Playlist[index]; }

		void AddAudioSource(AssetHandle& audio)
		{
			AudioSourceData.Playlist.emplace_back(audio);
			AudioSourceData.NumberOfAudioSources = (uint32_t)AudioSourceData.Playlist.size();
		}

		void RemoveAudioSource(uint32_t index)
		{
			AudioSourceData.Playlist.erase(AudioSourceData.Playlist.begin() + index);
			AudioSourceData.Playlist.shrink_to_fit();
			AudioSourceData.NumberOfAudioSources = (uint32_t)AudioSourceData.Playlist.size();
		}

		void RemoveAudioSource(AssetHandle& audio)
		{
			uint32_t index = 0;

			for (uint32_t i = 0; i < AudioSourceData.Playlist.size(); i++)
			{
				AssetHandle audioSource = AudioSourceData.Playlist[i];

				if (audioSource == audio)
				{
					index = i;
				}
			}

			AudioSourceData.Playlist.erase(AudioSourceData.Playlist.begin() + index);
			AudioSourceData.Playlist.shrink_to_fit();
			AudioSourceData.NumberOfAudioSources = (uint32_t)AudioSourceData.Playlist.size();
		}
	};

	struct AudioListenerComponent
	{
		bool Active = true;
		AudioListenerConfig Config;

		Ref<AudioListener> Listener;
	};

	// ============================================================================
	// 3D RENDERING COMPONENTS
	// ============================================================================

	struct MeshComponent
	{
		AssetHandle Mesh = 0;

		MeshComponent() = default;
		MeshComponent(const MeshComponent&) = default;
		MeshComponent(AssetHandle mesh)
			: Mesh(mesh) {}
	};

	struct MeshTagComponent
	{
		UUID MeshEntity = 0;

		MeshTagComponent() = default;
		MeshTagComponent(const MeshTagComponent&) = default;
		MeshTagComponent(UUID meshEntity)
			: MeshEntity(meshEntity) {
		}
	};

	struct PrefabComponent
	{
		AssetHandle PrefabID = 0;
		UUID EntityID = 0;

		PrefabComponent() = default;
		PrefabComponent(const PrefabComponent&) = default;
	};

	struct StaticMeshComponent
	{
		AssetHandle StaticMesh = 0;
		Ref<Lux::MaterialTable> MaterialTable = Ref<Lux::MaterialTable>::Create();
		bool Visible = true;

		StaticMeshComponent() = default;
		StaticMeshComponent(const StaticMeshComponent& other)
			: StaticMesh(other.StaticMesh), MaterialTable(Ref<Lux::MaterialTable>::Create(other.MaterialTable)), Visible(other.Visible)
		{
		}
		StaticMeshComponent(AssetHandle staticMesh)
			: StaticMesh(staticMesh) {}
	};

	struct SubmeshComponent
	{
		AssetHandle Mesh = 0;
		Ref<Lux::MaterialTable> MaterialTable = Ref<Lux::MaterialTable>::Create();
		std::vector<UUID> BoneEntityIds;
		uint32_t SubmeshIndex = 0;
		bool Visible = true;

		SubmeshComponent() = default;
		SubmeshComponent(const SubmeshComponent& other)
			: Mesh(other.Mesh), MaterialTable(Ref<Lux::MaterialTable>::Create(other.MaterialTable)), BoneEntityIds(other.BoneEntityIds), SubmeshIndex(other.SubmeshIndex), Visible(other.Visible)
		{
		}
		SubmeshComponent(AssetHandle mesh, uint32_t submeshIndex = 0)
			: Mesh(mesh), SubmeshIndex(submeshIndex) {}
	};

	// Physical unit a light's Intensity is expressed in. Unitless preserves the
	// legacy behaviour (Intensity used directly as a multiplier) so existing
	// scenes are unaffected until an artist opts into physical units.
	enum class LightUnit : uint32_t
	{
		Unitless = 0, // legacy multiplier
		Lux = 1,      // directional illuminance
		Lumens = 2,   // point / spot luminous power
		Candela = 3   // point / spot luminous intensity
	};

	struct DirectionalLightComponent
	{
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		LightUnit Unit = LightUnit::Unitless; // Unitless or Lux
		float ColorTemperature = 6500.0f;     // Kelvin
		bool UseColorTemperature = false;
		float ShadowAmount = 1.0f;
		bool CastShadows = true;
		bool SoftShadows = true;
		float LightSize = 0.5f;
		float ShadowDistance = 0.0f; // 0 = renderer max distance
		uint32_t ShadowResolutionTier = 2;

		DirectionalLightComponent() = default;
		DirectionalLightComponent(const DirectionalLightComponent&) = default;
	};

	struct PointLightComponent
	{
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		LightUnit Unit = LightUnit::Unitless; // Unitless, Lumens or Candela
		float ColorTemperature = 6500.0f;     // Kelvin
		bool UseColorTemperature = false;
		float LightSize = 0.5f;
		float MinRadius = 1.0f;
		float Radius = 10.0f;
		bool CastsShadows = true;
		bool SoftShadows = true;
		float Falloff = 1.0f;

		PointLightComponent() = default;
		PointLightComponent(const PointLightComponent&) = default;
	};

	struct SpotLightComponent
	{
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		LightUnit Unit = LightUnit::Unitless; // Unitless, Lumens or Candela
		float ColorTemperature = 6500.0f;     // Kelvin
		bool UseColorTemperature = false;
		float Range = 10.0f;
		float Angle = 45.0f;
		float AngleAttenuation = 1.0f;
		float Falloff = 1.0f;
		bool CastsShadows = false;
		bool SoftShadows = false;
		float ShadowDistance = 0.0f; // 0 = Range
		uint32_t ShadowResolutionTier = 1;

		SpotLightComponent() = default;
		SpotLightComponent(const SpotLightComponent&) = default;
	};

	struct SkyLightComponent
	{
		AssetHandle SceneEnvironment = 0;
		float Intensity = 1.0f;
		float Lod = 0.0f;
		bool DynamicSky = false;
		glm::vec3 TurbidityAzimuthInclination = { 2.0f, 0.0f, 0.0f };

		SkyLightComponent() = default;
		SkyLightComponent(const SkyLightComponent&) = default;
	};

	// ============================================================================

	template<typename... Component>
	struct ComponentGroup
	{
	};

	using AllComponents =
		ComponentGroup<TransformComponent, RelationshipComponent, SpriteRendererComponent,
		CircleRendererComponent, CameraComponent, ScriptComponent,
		NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent,
		CircleCollider2DComponent, RigidBodyComponent, CharacterControllerComponent, CompoundColliderComponent, BoxColliderComponent,
		SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent,
		TextComponent,
		MeshComponent, MeshTagComponent, PrefabComponent, StaticMeshComponent, SubmeshComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent>;

}
