#pragma once


#include "StarEngine/Core/UUID.h"
#include "StarEngine/Core/Ref.h"
#include "StarEngine/Audio/AudioListener.h"
#include "StarEngine/Audio/AudioSource.h"
#include "StarEngine/Renderer/Texture.h"
#include "StarEngine/Renderer/Font.h"
#include "SceneCamera.h"

#include "StarEngine/Asset/AssetManager.h"
#include "StarEngine/Scripting/CSharpObject.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "glm/gtx/quaternion.hpp"

#include <fstream>

namespace StarEngine {

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
		uint64_t ID = 0;

		IDComponent() = default;
		IDComponent(const uint64_t& id)
		{
			ID = id;
		}
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
		enum class Type { None = -1, Perspective, Orthographic };
		Type ProjectionType;

		SceneCamera Camera;
		bool Primary = true;

		CameraComponent() = default;
		CameraComponent(const CameraComponent& other) = default;

		operator SceneCamera& () { return Camera; }
		operator const SceneCamera& () const { return Camera; }
	};


	struct ScriptComponent
	{
		UUID ScriptID = 0;
		CSharpObject Instance;
		std::vector<uint32_t> FieldIDs;

		// NOTE(Peter): Gets set to true when OnCreate has been called for this entity
		bool IsRuntimeInitialized = false;
	};


	// Physics

	struct RigidBody2DComponent
	{
		enum class Type { None = -1, Static, Dynamic, Kinematic };
		Type BodyType;
		bool FixedRotation = false;
		float Mass = 1.0f;
		float LinearDrag = 0.01f;
		float AngularDrag = 0.05f;
		float GravityScale = 1.0f;
		bool IsBullet = false;
		// Storage for runtime
		void* RuntimeBody = nullptr;

		RigidBody2DComponent() = default;
		RigidBody2DComponent(const RigidBody2DComponent& other) = default;
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
	/*
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

		RigidBodyComponent() = default;
		RigidBodyComponent(const RigidBodyComponent& other) = default;
	};*/

	struct TextComponent
	{
		std::string TextString;
		Ref<Font> FontAsset = Font::GetDefault();
		glm::vec4 Color{ 1.0f };
		float Kerning = 0.0f;
		float LineSpacing = 0.0f;
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

	template<typename... Component>
	struct ComponentGroup
	{
	};

	using AllComponents =
		ComponentGroup<TransformComponent, SpriteRendererComponent,
		CircleRendererComponent, CameraComponent, ScriptComponent,
		RigidBody2DComponent, BoxCollider2DComponent,
		CircleCollider2DComponent, TextComponent, AudioData, AudioSourceComponent, AudioListenerComponent>;

}
