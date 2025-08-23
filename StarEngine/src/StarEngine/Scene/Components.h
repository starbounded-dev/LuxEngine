#pragma once
#define GLM_ENABLE_EXPERIMENTAL

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

#include "StarEngine/Math/Math.h"

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
		TagComponent(const TagComponent& other) = default;
		TagComponent(const std::string& tag)
			: Tag(tag) {
		}

		operator std::string& () { return Tag; }
		operator const std::string& () const { return Tag; }
	};

	struct RelationshipComponent
	{
		UUID ParentHandle = 0;
		std::vector<UUID> Children;

		RelationshipComponent() = default;
		RelationshipComponent(const RelationshipComponent& other) = default;
		RelationshipComponent(UUID parent)
			: ParentHandle(parent) {
		}
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
		TransformComponent(const TransformComponent& other) = default;
		TransformComponent(const glm::vec3& translation)
			: Translation(translation)
		{}

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

		// Make this method public
		glm::vec3 GetRotationEuler() const
		{
			return RotationEuler;
		}

		void SetRotationEuler(const glm::vec3& euler)
		{
			RotationEuler = euler;
			Rotation = glm::quat(RotationEuler);
		}

		glm::quat GetRotation() const
		{
			return Rotation;
		}

		void SetRotation(const glm::quat& quat)
		{
			auto wrapToPi = [](glm::vec3 v)
			{
				return glm::mod(v + glm::pi<float>(), 2.0f * glm::pi<float>()) - glm::pi<float>();
			};

			auto originalEuler = RotationEuler;
			Rotation = quat;
			RotationEuler = glm::eulerAngles(Rotation);

			glm::vec3 alternate1 = { RotationEuler.x - glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z - glm::pi<float>() };
			glm::vec3 alternate2 = { RotationEuler.x + glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z - glm::pi<float>() };
			glm::vec3 alternate3 = { RotationEuler.x + glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z + glm::pi<float>() };
			glm::vec3 alternate4 = { RotationEuler.x - glm::pi<float>(), glm::pi<float>() - RotationEuler.y, RotationEuler.z + glm::pi<float>() };

			float distance0 = glm::length2(wrapToPi(RotationEuler - originalEuler));
			float distance1 = glm::length2(wrapToPi(alternate1 - originalEuler));
			float distance2 = glm::length2(wrapToPi(alternate2 - originalEuler));
			float distance3 = glm::length2(wrapToPi(alternate3 - originalEuler));
			float distance4 = glm::length2(wrapToPi(alternate4 - originalEuler));

			float best = distance0;
			if (distance1 < best)
			{
				best = distance1;
				RotationEuler = alternate1;
			}
			if (distance2 < best)
			{
				best = distance2;
				RotationEuler = alternate2;
			}
			if (distance3 < best)
			{
				best = distance3;
				RotationEuler = alternate3;
			}
			if (distance4 < best)
			{
				best = distance4;
				RotationEuler = alternate4;
			}

			RotationEuler = wrapToPi(RotationEuler);
		}

		// Add SceneSerializer as a friend
		friend class SceneSerializer;
	};



	struct SpriteRendererComponent
	{
		glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
		AssetHandle Texture = 0;
		float TilingFactor = 1.0f;
		glm::vec2 UVStart{ 0.0f, 0.0f };
		glm::vec2 UVEnd{ 1.0f, 1.0f };
		bool ScreenSpace = false;

		SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent& other) = default;
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
		bool FixedAspectRatio = false; // Add this member

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
		glm::vec2 Offset = { 0.0f,0.0f };
		glm::vec2 Size = { 0.5f, 0.5f };

		float Density = 1.0f;
		float Friction = 1.0f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		BoxCollider2DComponent() = default;
		BoxCollider2DComponent(const BoxCollider2DComponent& other) = default;
	};

	struct CircleCollider2DComponent
	{
		glm::vec2 Offset = { 0.0f,0.0f };
		float Radius = 1.0f;

		float Density = 1.0f;
		float Friction = 1.0f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		CircleCollider2DComponent() = default;
		CircleCollider2DComponent(const CircleCollider2DComponent& other) = default;
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
