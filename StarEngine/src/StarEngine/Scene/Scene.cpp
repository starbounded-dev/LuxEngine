#include "sepch.h"
#include "Scene.h"

#include "Entity.h"
#include "Prefab.h"

#include "Components.h"

#include "StarEngine/Core/Application.h"
#include "StarEngine/Core/Events/EditorEvents.h"

#include "StarEngine/Renderer/SceneRenderer.h"

#include "StarEngine/Asset/AssetManager.h"

#include "StarEngine/Renderer/Renderer2D.h"
#include "StarEngine/Physics/PhysicsSystem.h"
#include "StarEngine/Physics/PhysicsScene.h"
#include "StarEngine/Physics2D/Physics2D.h"
#include "StarEngine/Audio/AudioEngine.h"

#include "StarEngine/Math/Math.h"
#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Renderer/SceneRenderer.h"

#include "StarEngine/Debug/Profiler.h"

#include "StarEngine/Scripting/ScriptEngine.h"

#include "SceneSerializer.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// Box2D
#include <box2d/box2d.h>
#include <assimp/scene.h>

namespace StarEngine {
	std::unordered_map<UUID, Scene*> s_ActiveScenes;

	struct SceneComponent
	{
		UUID SceneID;
	};

	struct PhysicsSceneComponent
	{
		Ref<PhysicsScene> PhysicsWorld = nullptr;
	};

	namespace Utils {
		glm::mat4 Mat4FromAIMatrix4x4(const aiMatrix4x4& matrix);
	}

}
