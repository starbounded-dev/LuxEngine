// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "Lux/Audio/AudioGeometrySettings.h"

#include "Lux/Audio/AudioEventRef.h"

#define GLM_ENABLE_EXPERIMENTAL

#include "SceneCamera.h"

#include "Lux/Animation/Animator.h"
#include "Lux/Asset/Asset.h"
#include "Lux/Audio/AudioListener.h"
#include "Lux/Audio/AcousticMaterial.h"
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

#include <concepts>
#include <type_traits>
#include <unordered_map>

namespace Lux {

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

		// Editor-only organizational state, serialized with the scene but ignored at runtime.
		// Locked: the entity can't be renamed, deleted, dragged, or edited in the editor.
		// LabelColor: a packed RGBA colour swatch drawn on the hierarchy row (0 == no label).
		bool Locked = false;
		uint32_t LabelColor = 0;

		// Runtime state, unlike the two above. Active == false switches the entity off together with
		// every descendant (Unity's activeSelf): renderers, lights, audio and scripts skip it; physics
		// does not honour it yet. Read it through Scene::IsEntityActiveInHierarchy, not directly.
		bool Active = true;

		// Free text scripts look entities up by (C# `Entity.FindWithTag`); empty for none. Unity's tag.
		// `Tag` above is the entity's name, kept under that name for older scenes and code.
		std::string UserTag;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& tag)
			: Tag(tag) {
		}

		operator std::string& () { return Tag; }
		operator const std::string& () const { return Tag; }
	};

	// Marks an entity as a purely organizational folder in the hierarchy: no gizmo, no editable
	// transform (its identity TransformComponent is kept so world-space math is untouched), children
	// are grouped under it without being moved. Editor-only in spirit but serialized like any marker.
	struct FolderComponent
	{
		// Placeholder so the type is non-empty: entt's empty-type optimization would otherwise make
		// emplace<>/get<> return void, which AddComponent/GetComponent can't bind to. Folders carry
		// no data of their own — this field is unused.
		uint8_t Reserved = 0;

		FolderComponent() = default;
		FolderComponent(const FolderComponent&) = default;
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
		bool Enabled = true;
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		AssetHandle Texture = 0;
		float TilingFactor = 1.0f;
		glm::vec2 UVStart = { 0.0f, 0.0f };
		glm::vec2 UVEnd = { 1.0f, 1.0f };
		bool ScreenSpace = false;
		// Nearest-neighbour sampling instead of linear: pixel art stays sharp when magnified.
		bool PointFilter = false;
		// A frame of a sprite sheet instead of Texture: the sheet's texture, the frame's rectangle,
		// sized from the frame (texels / the sheet's pixels per unit) and anchored at its pivot.
		AssetHandle Sheet = 0;
		int32_t SpriteIndex = 0;

		// Draw order: the project's sorting layer (a ProjectSortingLayer ID; 0 is "Default"), then the
		// order inside it, then depth. Serialized only when not the default.
		uint32_t SortingLayer = 0;
		int32_t OrderInLayer = 0;

		SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent&) = default;
		SpriteRendererComponent(const glm::vec4& color)
			: Color(color) {
		}
	};

	struct CircleRendererComponent
	{
		bool Enabled = true;
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float Thickness = 1.0f;
		float Fade = 0.005f;

		// Draw order: the project's sorting layer (a ProjectSortingLayer ID; 0 is "Default"), then the
		// order inside it, then depth. Serialized only when not the default.
		uint32_t SortingLayer = 0;
		int32_t OrderInLayer = 0;

		CircleRendererComponent() = default;
		CircleRendererComponent(const CircleRendererComponent&) = default;
	};

	// Plays one of the sprite sheet's clips on the SpriteRendererComponent next to it, in Play and Simulate.
	struct SpriteAnimatorComponent
	{
		bool Enabled = true;
		std::string Clip;       // a clip name in the sprite renderer's sheet; empty plays nothing
		bool Playing = true;    // false until a script plays a clip, or after a Once clip ends
		float Speed = 1.0f;

		// Runtime state, reset by the scene copy that Play starts from.
		float Time = 0.0f;      // seconds into the clip
		bool Finished = false;  // a Once clip reached its end

		SpriteAnimatorComponent() = default;
		SpriteAnimatorComponent(const SpriteAnimatorComponent&) = default;
	};

	// One layer of a tilemap: tiles by cell, drawn in list order (later on top). A cell holds the index of a frame of
	// the tilemap's tile set. See Tilemap.h to edit one.
	struct TilemapLayer
	{
		std::string Name = "Layer";
		bool Visible = true;
		bool Collides = true; // the solid tiles of this layer block bodies (the entity needs a Rigidbody 2D)
		std::unordered_map<int64_t, int32_t> Cells; // Tilemap::Key(x, y) -> frame index

		TilemapLayer() = default;
		explicit TilemapLayer(std::string name) : Name(std::move(name)) {}
	};

	// A grid of tiles from a sprite sheet (its frames are the tiles; a frame flagged Solid collides), in layers. Cell
	// (0, 0) has its lower left corner at the entity's origin; one cell is CellSize world units, scaled by the entity.
	// Paint it with the Tile Palette panel. With a static Rigidbody 2D on the same entity, solid tiles collide.
	struct TilemapComponent
	{
		bool Enabled = true;
		AssetHandle TileSet = 0;                    // a sprite sheet
		glm::vec2 CellSize = { 1.0f, 1.0f };
		std::vector<TilemapLayer> Layers = { TilemapLayer("Ground") };
		bool PointFilter = false;                   // nearest-neighbour sampling, for pixel-art tiles
		uint32_t SortingLayer = 0;                  // like a sprite's: the project's sorting layer ID and the order in it
		int32_t OrderInLayer = 0;

		// How the solid tiles feel; the same fields as a collider.
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;
		bool IsTrigger = false;

		TilemapComponent() = default;
		TilemapComponent(const TilemapComponent&) = default;
	};

	struct CameraComponent
	{
		enum class Type { None = -1, Perspective, Orthographic };

		Type ProjectionType = Type::Perspective;
		SceneCamera Camera;
		bool Primary = true; // TODO: think about moving to Scene
		bool FixedAspectRatio = false;

		// 2D games: fill the view with BackgroundColor instead of the sky. It is drawn in the 2D overlay,
		// after tone mapping, so the colour shows exactly as authored.
		bool SolidBackground = false;
		glm::vec4 BackgroundColor{ 0.12f, 0.13f, 0.16f, 1.0f };

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;

		operator SceneCamera& () { return Camera; }
		operator const SceneCamera& () const { return Camera; }
	};

	// Forward declaration
	class ScriptableEntity;

	struct ScriptComponent
	{
		bool Enabled = true; // false: OnUpdate is skipped, and OnCreate waits until the script is first enabled
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
		// A trigger detects overlaps (OnTriggerBegin/End) and blocks nothing.
		bool IsTrigger = false;

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
		// A trigger detects overlaps (OnTriggerBegin/End) and blocks nothing.
		bool IsTrigger = false;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		CircleCollider2DComponent() = default;
		CircleCollider2DComponent(const CircleCollider2DComponent&) = default;
	};

	// A convex polygon of 3 to 8 points, in the entity's local space (scaled by its X/Y scale). Points that are not
	// convex are replaced by their convex hull when the body is made.
	struct PolygonCollider2DComponent
	{
		static constexpr size_t MaxPoints = 8; // b2_maxPolygonVertices

		std::vector<glm::vec2> Points = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
		glm::vec2 Offset = { 0.0f, 0.0f };

		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;
		bool IsTrigger = false;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		PolygonCollider2DComponent() = default;
		PolygonCollider2DComponent(const PolygonCollider2DComponent&) = default;
	};

	// A rounded rectangle: a box with a half circle on each end of its long side. Box2D has no capsule, so it
	// is built from a box and two circles on the body.
	struct CapsuleCollider2DComponent
	{
		enum class Direction { Vertical = 0, Horizontal };

		glm::vec2 Offset = { 0.0f, 0.0f };
		float Radius = 0.25f; // of the rounded ends: half the width
		float Height = 1.0f;  // the whole length, rounded ends included (never less than twice the radius)
		Direction CapsuleDirection = Direction::Vertical;

		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;
		bool IsTrigger = false;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		CapsuleCollider2DComponent() = default;
		CapsuleCollider2DComponent(const CapsuleCollider2DComponent&) = default;
	};

	// A line of two or more points that blocks (a floor, a wall, terrain), open or closed. It has no area and
	// no mass: it belongs on a static or kinematic body. Points are in local space, scaled by the entity's X/Y scale.
	struct EdgeCollider2DComponent
	{
		std::vector<glm::vec2> Points = { { -1.0f, 0.0f }, { 1.0f, 0.0f } };
		bool Loop = false; // joins the last point to the first (needs three or more)
		glm::vec2 Offset = { 0.0f, 0.0f };

		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;
		bool IsTrigger = false;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		EdgeCollider2DComponent() = default;
		EdgeCollider2DComponent(const EdgeCollider2DComponent&) = default;
	};

	// Joins this entity's body to another body, or to a fixed point in the world. Made when the scene starts.
	struct Joint2DComponent
	{
		enum class Type { Distance = 0, Revolute, Prismatic, Weld };

		bool Enabled = true;
		Type JointType = Type::Distance;
		UUID ConnectedEntity = 0;           // the other body; 0 anchors this body to the world
		glm::vec2 Anchor = { 0.0f, 0.0f };  // on this body, in its local space
		glm::vec2 ConnectedAnchor = { 0.0f, 0.0f }; // Distance only: on the other body (local), or a world point when connected to the world
		bool CollideConnected = false;      // whether the two bodies still collide with each other

		// Distance: keeps the anchors a length apart; a stiffness makes it a spring.
		bool AutoLength = true;             // the distance at the start
		float Length = 1.0f;
		float Stiffness = 0.0f;             // N/m; 0 is a rigid rod
		float Damping = 0.0f;

		// Revolute (a hinge about Anchor) and Prismatic (a slider along Axis): limits and a motor.
		bool EnableLimit = false;
		float LowerLimit = 0.0f;            // degrees (revolute) or units (prismatic)
		float UpperLimit = 0.0f;
		bool EnableMotor = false;
		float MotorSpeed = 0.0f;            // degrees per second, or units per second
		float MaxMotorForce = 100.0f;       // N*m (revolute) or N (prismatic)
		glm::vec2 Axis = { 1.0f, 0.0f };    // Prismatic: the slide direction in this body's local space

		// Storage for runtime
		void* RuntimeJoint = nullptr;

		Joint2DComponent() = default;
		Joint2DComponent(const Joint2DComponent&) = default;
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
		static constexpr uint32_t AllSubmeshes = UINT32_MAX;

		AssetHandle ColliderAsset = 0;
		uint32_t SubmeshIndex = AllSubmeshes;   // one part of a multi-part mesh, or AllSubmeshes
		bool UseSharedShape = false;
		ColliderMaterial Material;
		AcousticMaterial Acoustic = AcousticMaterial::Default;
		// Use the render material's acoustic tag when it has one; Acoustic is the fallback. New
		// colliders inherit; scenes saved before this field keep their explicit tags.
		bool AcousticFromMaterial = true;
		AcousticGeometryMode AcousticMotion = AcousticGeometryMode::Static;
		ECollisionComplexity CollisionComplexity = ECollisionComplexity::Default;

		MeshColliderComponent() = default;
		MeshColliderComponent(const MeshColliderComponent&) = default;
	};

	struct TextComponent
	{
		bool Enabled = true;
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

		// Draw order: the project's sorting layer (a ProjectSortingLayer ID; 0 is "Default"), then the
		// order inside it, then depth. Serialized only when not the default.
		uint32_t SortingLayer = 0;
		int32_t OrderInLayer = 0;

		TextComponent() = default;
		TextComponent(const TextComponent& other) = default;
	};


	enum class AudioZoneShape : uint8_t { Box, Sphere, Collider };
	using AudioSnapshotRef = AudioEventRef;

	// Authored startup settings. Runtime controls belong to the scene's MusicDirector.
	struct MusicDirectorComponent
	{
		AudioEventRef Event;
		bool PlayOnAwake = true;
		std::string InitialState;
		float Intensity = 0.0f;
	};

	struct AudioZoneComponent
	{
		bool Enabled = true;
		AudioZoneShape Shape = AudioZoneShape::Box;
		glm::vec3 Offset{ 0.0f };
		glm::vec3 HalfExtents{ 5.0f, 3.0f, 5.0f };
		float Radius = 5.0f;
		float Priority = 0.0f;
		float BlendDistance = 2.0f;
		float FadeTime = 0.25f;
		float Volume = 1.0f;
		AudioEventRef AmbienceEvent;
		AudioSnapshotRef Snapshot;
	};

	struct AudioPortalComponent
	{
		bool Enabled = true;
		UUID ZoneA = 0, ZoneB = 0;
		glm::vec3 HalfExtents{ 0.5f, 1.0f, 0.05f };
		float Open = 0.0f;
		float BlendDistance = 5.0f;
		AcousticMaterial Material = AcousticMaterial::Wood;
	};

	struct AudioSurfaceComponent
	{
		// Overrides the acoustic tag on this entity’s mesh collider when present.
		AcousticMaterial Material = AcousticMaterial::Default;
		AudioEventRef FootstepOverride;
		AudioEventRef ImpactOverride;
		bool PhysicsSounds = true;
		bool AutoFootsteps = false;
		float StrideLength = 0.7f;
		float GroundProbeDistance = 1.2f;
		float FootstepWeight = 75.0f;
	};

	struct AudioSourceConfig
	{
		float VolumeMultiplier = 1.0f;
		float PitchMultiplier = 1.0f;
		bool PlayOnAwake = true;
	};

	struct AudioSourceComponent
	{
		bool Enabled = true; // false: the event never starts; switching it off during Play stops it
		AudioSourceConfig Config;
		int Priority = 128; // voice priority: 0 is highest, 256 is lowest.
		bool DistanceCulling = false; // Opt in: distant loops suspend; one-shots are discarded.

		// The Sound this source plays: spatialization, attenuation, randomization and the bus come from
		// the Sound as authored.
		AudioEventRef Event;

		// Parameters applied when the instance is created, so two entities can share one Sound and
		// still sound different - a large and a small door, one alarm that is more urgent than another.
		// Names must match parameters on the Sound; anything else is ignored rather than failing.
		std::vector<std::pair<std::string, float>> ParameterOverrides;

		// Migration data only. Preserved on save; never used for playback.
		AssetHandle LegacyAudio = 0;
		bool LegacyLooping = false;

		// Runtime-only script pause, layered over scene pause by AudioEventInstance.
		bool ScriptPaused = false;
	};

	struct AudioListenerComponent
	{
		bool Active = true;
		int ListenerIndex = 0;
		float Weight = 1.0f;
		bool UseAttenuationTarget = false;
		UUID AttenuationTarget = 0;
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
		// On an instance root only, by prefab entity UUID: children the user deleted from this instance,
		// and children the user moved (within it or out of it). Scene::SyncPrefabInstance leaves them be.
		std::vector<UUID> RemovedEntities;
		std::vector<UUID> ReparentedEntities;
		// The records above are kept for this instance. False on instances saved before they existed:
		// their first sync turns what they lack or have moved into records.
		bool Tracked = false;

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

	// Makes a static-mesh entity skinned: the mesh's bones are driven by this skeleton's pose (the
	// renderer's skinned draw path, R12). The pose itself is computed at runtime, not stored.
	struct SkeletonComponent
	{
		bool Enabled = true;           // false: the mesh draws unskinned
		AssetHandle SkeletonAsset = 0; // the .lskel whose bones the mesh's bone names are resolved against
		// Editor preview until animation playback exists: bend one bone (by name) about its local X axis.
		std::string DebugBone;
		float DebugBendDegrees = 0.0f;

		// Playback of one clip (Animation asset). With no Clip the mesh holds the rest pose (plus the debug bend).
		// These are authored and saved; what playback changes lives in State and is never saved.
		AssetHandle Clip = 0;
		AssetHandle Graph = 0;     // an AnimationGraph that picks the clip (and Speed and Loop per state); Clip is then ignored
		float StartTime = 0.0f;    // seconds into the clip where playback starts (or holds, when not playing on start)
		float Speed = 1.0f;        // playback rate; negative plays backwards
		bool Loop = true;
		bool PlayOnStart = true;   // false: hold the pose at StartTime until something plays it
		float FadeSeconds = 0.25f; // cross-fade length when Clip is changed while playing
		bool RootMotion = false;   // in a running scene, move the entity by the root bone's horizontal travel instead of the mesh
		std::string RootBone;      // bone whose travel is the root motion (default: the first bone with a position track)

		AnimatorState State;       // runtime only

		// Starts `clip` from its beginning and fades from what was playing over `seconds` (0: switch at once).
		void CrossFadeTo(AssetHandle clip, float seconds)
		{
			StartCrossFade(State, Clip, seconds);
			Clip = clip;
		}
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
		bool Enabled = true; // false: the light contributes nothing, shadows included
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
		bool Enabled = true; // false: the light contributes nothing, shadows included
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		LightUnit Unit = LightUnit::Unitless; // Unitless, Lumens or Candela
		float ColorTemperature = 6500.0f;     // Kelvin
		bool UseColorTemperature = false;
		float LightSize = 0.5f;
		float MinRadius = 1.0f; // unused since physical (inverse-square) falloff; still serialized
		float Radius = 10.0f;   // range: the falloff window reaches zero here
		bool CastsShadows = true;
		bool SoftShadows = true;
		float Falloff = 1.0f;   // unused since physical (inverse-square) falloff; still serialized

		PointLightComponent() = default;
		PointLightComponent(const PointLightComponent&) = default;
	};

	struct SpotLightComponent
	{
		bool Enabled = true; // false: the light contributes nothing, its shadow tile included
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		LightUnit Unit = LightUnit::Unitless; // Unitless, Lumens or Candela
		float ColorTemperature = 6500.0f;     // Kelvin
		bool UseColorTemperature = false;
		float Range = 10.0f;
		float Angle = 45.0f;
		float AngleAttenuation = 1.0f;
		float Falloff = 1.0f; // emitter diameter in metres for PCSS soft shadows ("Light Size" in the editor; key kept for scenes)
		bool CastsShadows = false;
		bool SoftShadows = false;
		float ShadowDistance = 0.0f; // 0 = Range
		uint32_t ShadowResolutionTier = 1;

		SpotLightComponent() = default;
		SpotLightComponent(const SpotLightComponent&) = default;
	};

	// AreaLightComponent::Shape; mirrors AREA_LIGHT_SHAPE_* in LTC.glslh.
	enum class AreaLightShape : uint32_t
	{
		Rect = 0,
		Disk = 1,
		Sphere = 2,
		Tube = 3
	};

	// A light with a surface, lit with Linearly Transformed Cosines. A rectangle (Width x Height, in
	// the local X/Y plane) or a disk (Radius) emits along the entity's -Z; a sphere (Radius) and a tube
	// (Length along local X, Radius) emit all around. The entity's scale stretches the shape.
	// CastsShadows renders the light into the spot shadow atlas (a rect or disk one tile per lit side,
	// a sphere or tube six cube faces); SoftShadows sizes the penumbra from the shape (PCSS).
	struct AreaLightComponent
	{
		bool Enabled = true; // false: the light contributes nothing, its shadow tile included
		AreaLightShape Shape = AreaLightShape::Rect;
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		LightUnit Unit = LightUnit::Unitless; // Unitless (luminance) or Lumens
		float ColorTemperature = 6500.0f;     // Kelvin
		bool UseColorTemperature = false;
		float Width = 1.0f;  // Rect
		float Height = 1.0f; // Rect
		float Radius = 0.5f; // Disk, Sphere, Tube
		float Length = 1.0f; // Tube
		bool TwoSided = false; // Rect, Disk
		float Range = 10.0f; // the light's influence fades to zero at this distance from its centre
		bool CastsShadows = false;
		bool SoftShadows = true;

		AreaLightComponent() = default;
		AreaLightComponent(const AreaLightComponent&) = default;
	};

	struct SkyLightComponent
	{
		bool Enabled = true; // false: the scene falls back to the default environment
		AssetHandle SceneEnvironment = 0;
		float Intensity = 1.0f;
		float Lod = 0.0f;
		float Rotation = 0.0f; // degrees about +Y; turns the sky and the lighting it gives together
		bool DynamicSky = false;
		glm::vec3 TurbidityAzimuthInclination = { 2.0f, 0.0f, 0.0f };

		SkyLightComponent() = default;
		SkyLightComponent(const SkyLightComponent&) = default;
	};

	// ============================================================================

	// Components with an on/off switch (Unity's per-component enabled): the editor draws a checkbox in
	// their header and C# reads and writes it as `Component.Enabled`. Meshes keep their older Visible
	// name, the audio listener its Active.
	template<typename TComponent>
	constexpr bool HasEnabledFlag = requires(TComponent& c) { { c.Enabled } -> std::same_as<bool&>; }
		|| requires(TComponent& c) { { c.Visible } -> std::same_as<bool&>; }
		|| std::is_same_v<TComponent, AudioListenerComponent>;

	template<typename TComponent>
	bool& GetEnabledFlag(TComponent& component)
	{
		if constexpr (std::is_same_v<TComponent, AudioListenerComponent>)
			return component.Active;
		else if constexpr (requires { component.Visible; })
			return component.Visible;
		else
			return component.Enabled;
	}

	template<typename... Component>
	struct ComponentGroup
	{
	};

	using AllComponents =
		ComponentGroup<TransformComponent, RelationshipComponent, SpriteRendererComponent, SpriteAnimatorComponent,
		PolygonCollider2DComponent, CapsuleCollider2DComponent, EdgeCollider2DComponent, Joint2DComponent, TilemapComponent, CircleRendererComponent, CameraComponent, ScriptComponent,
		NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent,
		CircleCollider2DComponent, RigidBodyComponent, CharacterControllerComponent, CompoundColliderComponent, BoxColliderComponent,
		SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent,
		TextComponent,
		MeshComponent, MeshTagComponent, PrefabComponent, StaticMeshComponent, SkeletonComponent, SubmeshComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, AreaLightComponent, SkyLightComponent,
		AudioSourceComponent, AudioListenerComponent, AudioSurfaceComponent, AudioZoneComponent, AudioPortalComponent, MusicDirectorComponent,
		FolderComponent>;

}
