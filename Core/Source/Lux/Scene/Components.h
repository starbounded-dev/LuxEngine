#pragma once

#include "SceneCamera.h"


#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioListener.h"
#include "Lux/Audio/AudioSource.h"
#include "Lux/Core/UUID.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/UI/Font.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL

#include "glm/gtx/quaternion.hpp"

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
	};

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& translation)
			: Translation(translation) {
		}

		glm::mat4 GetTransform() const
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

			return glm::translate(glm::mat4(1.0f), Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), Scale);
		}
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
		SceneCamera Camera;
		bool Primary = true; // TODO: think about moving to Scene
		bool FixedAspectRatio = false;

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;
	};

	// Forward declaration
	class ScriptableEntity;

	struct ScriptComponent
	{
		std::string ClassName;

		// Add the missing Instance member
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
		enum class BodyType
		{
			Static = 0, Dynamic, Kinematic
		};

		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

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

	struct TextComponent
	{
		std::string TextString = "";
		size_t TextHash = 0;

		// Font
		AssetHandle FontHandle;
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

		Ref<AudioSource> GetAudioSource(uint32_t index) const { return AssetManager::GetAsset<AudioSource>(AudioSourceData.Playlist[index]); }
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
		AssetHandle MaterialTable = 0;
		bool Visible = true;
		bool CastShadows = true;

		MeshComponent() = default;
		MeshComponent(const MeshComponent&) = default;
	};

	struct MeshTagComponent
	{
		std::string MeshName;

		MeshTagComponent() = default;
		MeshTagComponent(const MeshTagComponent&) = default;
		MeshTagComponent(const std::string& meshName)
			: MeshName(meshName) {
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
		AssetHandle Mesh = 0;
		AssetHandle MaterialTable = 0;
		bool Visible = true;
		bool CastShadows = true;

		StaticMeshComponent() = default;
		StaticMeshComponent(const StaticMeshComponent&) = default;
	};

	struct DirectionalLightComponent
	{
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		float ShadowAmount = 1.0f;
		bool CastShadows = true;
		bool SoftShadows = true;
		float LightSize = 0.5f;

		DirectionalLightComponent() = default;
		DirectionalLightComponent(const DirectionalLightComponent&) = default;
	};

	struct PointLightComponent
	{
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		float Radius = 10.0f;
		float Falloff = 1.0f;
		float MinRadius = 0.001f;
		float LightSize = 0.5f;
		bool CastShadows = false;
		bool SoftShadows = true;

		PointLightComponent() = default;
		PointLightComponent(const PointLightComponent&) = default;
	};

	struct SpotLightComponent
	{
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		float Range = 10.0f;
		float Angle = 45.0f;
		float AngleAttenuation = 1.0f;
		float Falloff = 1.0f;
		bool CastShadows = false;
		bool SoftShadows = false;

		SpotLightComponent() = default;
		SpotLightComponent(const SpotLightComponent&) = default;
	};

	struct SkyLightComponent
	{
		AssetHandle EnvironmentMap = 0;
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
		CircleCollider2DComponent, TextComponent, AudioData, AudioSourceComponent, AudioListenerComponent,
		MeshComponent, MeshTagComponent, PrefabComponent, StaticMeshComponent, DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent>;

}
