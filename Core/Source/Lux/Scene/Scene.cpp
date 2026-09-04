#include "lpch.h"

#include "Lux/Scene/Scene.h"

#include "Lux/Asset/AssetManager.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioSource.h"
#include "Lux/Audio/AudioListener.h"

#include "Lux/Core/JobSystem.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scene/ScriptableEntity.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Renderer/FrameRenderPacket.h"
#include "Lux/Renderer/PhysicalLight.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/MeshFactory.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Physics/PhysicsScene.h"
#include "Lux/Physics/PhysicsSystem.h"
#include "Lux/Physics2D/PhysicsScene2D.h"
#include "Lux/Project/Project.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace Lux {

	namespace
	{
		std::filesystem::path ResolveAudioFilePath(const Ref<AudioFile>& audioFile)
		{
			if (!audioFile)
				return {};

			const std::filesystem::path storedPath = audioFile->FilePath;
			if (storedPath.empty())
				return {};

			if (storedPath.is_absolute())
				return storedPath;

			if (Ref<Project> project = Project::GetActive())
				return project->GetAssetFileSystemPath(storedPath);

			return storedPath;
		}

		Ref<AudioSource> CreateRuntimeAudioSourceFromHandle(AssetHandle handle)
		{
			if (!AssetManager::IsAssetHandleValid(handle))
				return nullptr;

			Ref<AudioFile> audioFile = AssetManager::GetAsset<AudioFile>(handle);
			if (!audioFile)
				return nullptr;

			const std::filesystem::path filepath = ResolveAudioFilePath(audioFile);
			if (filepath.empty())
				return nullptr;

			Ref<AudioSource> audioSource = Ref<AudioSource>::Create();
			if (!audioSource->LoadFromFile(filepath))
				return nullptr;

			return audioSource;
		}

		enum class ColliderDebugPrimitive
		{
			Box,
			Sphere
		};

		AssetHandle GetColliderDebugPrimitiveMesh(ColliderDebugPrimitive primitive)
		{
			static AssetHandle s_BoxMesh = 0;
			static AssetHandle s_SphereMesh = 0;

			AssetHandle& handle = primitive == ColliderDebugPrimitive::Box ? s_BoxMesh : s_SphereMesh;
			if (handle && AssetManager::IsAssetHandleValid(handle))
				return handle;

			switch (primitive)
			{
				case ColliderDebugPrimitive::Box:
					handle = MeshFactory::CreateBox({ 1.0f, 1.0f, 1.0f });
					break;
				case ColliderDebugPrimitive::Sphere:
					handle = MeshFactory::CreateSphere(1.0f);
					break;
			}

			return handle;
		}

		uint64_t PackColliderDimension(float value)
		{
			const int64_t packed = (int64_t)std::llround(std::max(value, 0.001f) * 10000.0f);
			return (uint64_t)std::clamp<int64_t>(packed, 1, 0xffffffffll);
		}

		AssetHandle GetCapsuleColliderDebugMesh(float radius, float cylinderHeight)
		{
			static std::unordered_map<uint64_t, AssetHandle> s_CapsuleMeshes;

			radius = std::max(radius, 0.001f);
			cylinderHeight = std::max(cylinderHeight, 0.001f);

			const uint64_t key = (PackColliderDimension(radius) << 32) | PackColliderDimension(cylinderHeight);
			if (auto it = s_CapsuleMeshes.find(key); it != s_CapsuleMeshes.end())
			{
				if (it->second && AssetManager::IsAssetHandleValid(it->second))
					return it->second;
			}

			AssetHandle handle = MeshFactory::CreateCapsule(radius, cylinderHeight);
			s_CapsuleMeshes[key] = handle;
			return handle;
		}

		bool ResolveStaticMeshDebugAssets(AssetHandle handle, Ref<StaticMesh>& staticMesh, Ref<MeshSource>& meshSource)
		{
			staticMesh = StaticMesh::GetOrCreateRuntime(handle);
			if (!staticMesh)
				return false;

			meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource());
			return meshSource != nullptr;
		}

		glm::mat4 GetPhysicsColliderBodyTransform(const TransformComponent& worldTransform)
		{
			return glm::translate(glm::mat4(1.0f), worldTransform.Translation)
				* glm::toMat4(worldTransform.GetRotation());
		}

		AssetHandle ResolveMeshColliderHandle(Entity entity, const MeshColliderComponent& collider)
		{
			if (collider.ColliderAsset)
				return collider.ColliderAsset;

			if (entity.HasComponent<StaticMeshComponent>())
				return entity.GetComponent<StaticMeshComponent>().StaticMesh;

			return 0;
		}
	}

	Scene::Scene()
	{
		m_Renderer2D = Ref<Renderer2D>::Create();
	}

	Scene::~Scene()
	{
		ReleaseAllRuntimeAudio();
		m_EntityMap.clear();
		OnPhysics2DStop();
		OnPhysics3DStop();
	}

	template<typename... Component>
	static void CopyComponent(entt::registry& dst, entt::registry& src, const std::unordered_map<UUID, entt::entity>& enttMap)
	{
		([&]()
			{
				auto view = src.view<Component>();
				for (auto srcEntity : view)
				{
					entt::entity dstEntity = enttMap.at(src.get<IDComponent>(srcEntity).ID);

					auto& srcComponent = src.get<Component>(srcEntity);
					dst.emplace_or_replace<Component>(dstEntity, srcComponent);
				}
			}(), ...);
	}

	template<typename... Component>
	static void CopyComponent(ComponentGroup<Component...>, entt::registry& dst, entt::registry& src, const std::unordered_map<UUID, entt::entity>& enttMap)
	{
		CopyComponent<Component...>(dst, src, enttMap);
	}

	template<typename... Component>
	static void CopyComponentIfExists(Entity dst, Entity src)
	{
		([&]()
			{
				if (src.HasComponent<Component>())
					dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
			}(), ...);
	}

	template<typename... Component>
	static void CopyComponentIfExists(ComponentGroup<Component...>, Entity dst, Entity src)
	{
		CopyComponentIfExists<Component...>(dst, src);
	}

	Ref<Scene> Scene::Copy(Ref<Scene> other)
	{
		Ref<Scene> newScene = Ref<Scene>::Create();
		newScene->Handle = other->Handle;
		newScene->m_Name = other->m_Name;

		newScene->m_ViewportWidth = other->m_ViewportWidth;
		newScene->m_ViewportHeight = other->m_ViewportHeight;

		auto& srcSceneRegistry = other->m_Registry;
		auto& dstSceneRegistry = newScene->m_Registry;
		std::unordered_map<UUID, entt::entity> enttMap;

		// Create entities in new scene
		auto idView = srcSceneRegistry.view<IDComponent>();
		for (auto e : idView)
		{
			UUID uuid = srcSceneRegistry.get<IDComponent>(e).ID;
			const auto& srcTag = srcSceneRegistry.get<TagComponent>(e);
			Entity newEntity = newScene->CreateEntityWithUUID(uuid, srcTag.Tag);
			// TagComponent is created by name above; carry its editor-only lock/label state too.
			auto& dstTag = newEntity.GetComponent<TagComponent>();
			dstTag.Locked = srcTag.Locked;
			dstTag.LabelColor = srcTag.LabelColor;
			enttMap[uuid] = (entt::entity)newEntity;
		}

		// Copy components (except IDComponent and TagComponent)
		CopyComponent(AllComponents{}, dstSceneRegistry, srcSceneRegistry, enttMap);

		return newScene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithID(UUID(), name);
	}

	Entity Scene::CreateChildEntity(Entity parent, const std::string& name)
	{
		Entity entity = CreateEntity(name);
		if (parent)
			ParentEntity(entity, parent);
		return entity;
	}

	Entity Scene::CreateEntityWithID(UUID uuid, const std::string& name, bool shouldSort)
	{
		(void)shouldSort;
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		m_EntityMap[uuid] = entity;
		return entity;
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		return CreateEntityWithID(uuid, name);
	}

	void Scene::SubmitToDestroyEntity(Entity entity)
	{
		if (!entity)
			return;

		m_PostUpdateQueue.emplace_back([this, entityID = entity.GetUUID()]()
		{
			DestroyEntity(entityID);
		});
	}

	void Scene::DestroyEntity(Entity entity, bool excludeChildren, bool first)
	{
		if (!entity)
			return;

		if (!excludeChildren && entity.HasComponent<RelationshipComponent>())
		{
			const std::vector<UUID> children = entity.Children();
			for (UUID childID : children)
				DestroyEntity(childID, false, false);
		}

		if (Entity parent = entity.GetParent())
			parent.RemoveChild(entity);

		// Fire the managed OnDestroy before the entity leaves the registry, so scripts can
		// still read their own components. Only relevant while the runtime is playing.
		if (m_IsRunning && entity.HasComponent<ScriptComponent>())
		{
			UUID entityID = entity.GetUUID();
			auto it = m_ScriptInstances.find(entityID);
			if (it != m_ScriptInstances.end())
			{
				ScriptEngine& scriptEngine = ScriptEngine::GetMutable();
				const auto& sc = entity.GetComponent<ScriptComponent>();
				if (scriptEngine.IsValidScript(sc.ScriptID) &&
					scriptEngine.GetScriptMetadata(sc.ScriptID).HasMethod("OnDestroy"))
					it->second.Invoke("OnDestroy");

				scriptEngine.DestroyInstance(entityID, m_ScriptStorage);
				m_ScriptInstances.erase(it);
			}
		}

		ReleaseRuntimeAudio(entity);
		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);

		if (first)
			SortEntities();
	}

	void Scene::DestroyEntity(UUID entityID, bool excludeChildren, bool first)
	{
		DestroyEntity(TryGetEntityWithUUID(entityID), excludeChildren, first);
	}

	Ref<AudioSource> Scene::GetOrCreateRuntimeAudioSource(Entity entity, AssetHandle audioHandle)
	{
		if (!entity || !audioHandle)
			return nullptr;

		Ref<AudioSource>& runtimeAudio = m_RuntimeAudioSources[entity.GetUUID()];
		if (!runtimeAudio)
			runtimeAudio = CreateRuntimeAudioSourceFromHandle(audioHandle);

		return runtimeAudio;
	}

	Ref<AudioSource> Scene::GetOrCreateRuntimePlaylistSource(Entity entity, uint32_t index, AssetHandle audioHandle)
	{
		if (!entity || !audioHandle)
			return nullptr;

		auto& runtimePlaylist = m_RuntimeAudioPlaylists[entity.GetUUID()];
		if (runtimePlaylist.size() <= index)
			runtimePlaylist.resize(index + 1);

		if (!runtimePlaylist[index])
			runtimePlaylist[index] = CreateRuntimeAudioSourceFromHandle(audioHandle);

		return runtimePlaylist[index];
	}

	void Scene::ReleaseRuntimeAudio(Entity entity)
	{
		if (!entity)
			return;

		m_RuntimeAudioSources.erase(entity.GetUUID());
		m_RuntimeAudioPlaylists.erase(entity.GetUUID());
	}

	void Scene::ReleaseAllRuntimeAudio()
	{
		m_RuntimeAudioSources.clear();
		m_RuntimeAudioPlaylists.clear();
	}

	void Scene::OnRuntimeStart()
	{
		m_IsRunning = true;

		OnPhysics2DStart();
		OnPhysics3DStart();

		PhysicsScene2D::SetPlaying(true);

		{
			auto filter = m_Registry.view<TransformComponent, AudioListenerComponent>();
			filter.each([&](entt::entity entityHandle, TransformComponent&, AudioListenerComponent& ac)
				{
					ac.Listener = Ref<AudioListener>::Create();
					if (ac.Active)
					{
						Entity entity = { entityHandle, this };
						const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(entity);
						const glm::mat4 inverted = glm::inverse(worldTransform);
						const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
						const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));
						ac.Listener->SetConfig(ac.Config);
						ac.Listener->SetPosition(glm::vec4(worldPosition, 1.0f));
						ac.Listener->SetDirection(glm::vec3{ -forward.x, -forward.y, -forward.z });
					}
				});
		}

		{
			auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
			view.each([&](entt::entity entityHandle, TransformComponent&, AudioSourceComponent& ac)
				{
					if (AssetManager::IsAssetHandleValid(ac.Audio))
					{
						Entity entity = { entityHandle, this };
						const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(entity);
						const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
						const glm::mat4 inverted = glm::inverse(worldTransform);
						const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));

						if (ac.Audio && !ac.AudioSourceData.UsePlaylist)
						{
							Ref<AudioSource> audioSource = GetOrCreateRuntimeAudioSource(entity, ac.Audio);

							if (audioSource != nullptr)
							{
								audioSource->SetConfig(ac.Config);
								audioSource->SetPosition(glm::vec4(worldPosition, 1.0f));
								audioSource->SetDirection(forward);
								if (ac.Config.PlayOnAwake)
									audioSource->Play();
							}
						}
						else if (ac.Audio && ac.AudioSourceData.UsePlaylist)
						{
							if (ac.AudioSourceData.CurrentIndex >= ac.AudioSourceData.Playlist.size())
								ac.AudioSourceData.CurrentIndex = 0;

							if (ac.AudioSourceData.CurrentIndex < ac.AudioSourceData.Playlist.size())
							{
								Ref<AudioSource> playingSourceIndex = GetOrCreateRuntimePlaylistSource(entity, ac.AudioSourceData.CurrentIndex, ac.AudioSourceData.Playlist[ac.AudioSourceData.CurrentIndex]);

								if (playingSourceIndex != nullptr)
								{
									playingSourceIndex->SetConfig(ac.Config);
									playingSourceIndex->SetPosition(glm::vec4(worldPosition, 1.0f));
									playingSourceIndex->SetDirection(forward);
									if (ac.Config.PlayOnAwake)
										playingSourceIndex->Play();

									ac.AudioSourceData.PlayingCurrentIndex = true;
									ac.AudioSourceData.CurrentIndex++;
								}
							}
						}
					}
				});
		}
		// Scripting: instantiate every script entity and fire OnCreate.
		{
			ScriptEngine& scriptEngine = ScriptEngine::GetMutable();
			scriptEngine.SetCurrentScene(this);

			auto view = m_Registry.view<ScriptComponent>();
			for (auto e : view)
			{
				Entity entity = { e, this };
				const auto& sc = entity.GetComponent<ScriptComponent>();
				if (!scriptEngine.IsValidScript(sc.ScriptID))
				{
					// Without this the entity is skipped here AND every frame in OnUpdateRuntime
					// (which drops entities with no instance), so the script silently never runs.
					LUX_CORE_ERROR("[Scripting] Script '{}' (ID {}) is not present in the loaded app assembly - it will not run. "
						"Rebuild the script project and re-export so the assembly matches the scene.",
						sc.ClassName.empty() ? "<unnamed>" : sc.ClassName, (uint64_t)sc.ScriptID);
					continue;
				}

				UUID entityID = entity.GetUUID();
				if (!m_ScriptStorage.EntityStorage.contains(entityID))
					m_ScriptStorage.InitializeEntityStorage(sc.ScriptID, entityID);

				CSharpObject instance = scriptEngine.Instantiate(entityID, m_ScriptStorage, (uint64_t)entityID);
				m_ScriptInstances[entityID] = instance;

				if (scriptEngine.GetScriptMetadata(sc.ScriptID).HasMethod("OnCreate"))
					instance.Invoke("OnCreate");
			}
		}
	}

	void Scene::OnRuntimeStop()
	{
		m_IsRunning = false;

		PhysicsScene2D::SetPlaying(false);

		OnPhysics3DStop();
		OnPhysics2DStop();

		{
			auto view = m_Registry.view<AudioSourceComponent>();
			view.each([&](entt::entity entity, AudioSourceComponent& asc)
				{
					auto& ac = asc;
					if (AssetManager::IsAssetHandleValid(ac.Audio))
					{
						if (ac.Audio && !ac.AudioSourceData.UsePlaylist)
						{
							Ref<AudioSource> audioSource = GetOrCreateRuntimeAudioSource({ entity, this }, ac.Audio);

							if (audioSource != nullptr && audioSource->IsPlaying())
								audioSource->Stop();
						}
						else if (ac.Audio && ac.AudioSourceData.UsePlaylist)
						{
							ac.AudioSourceData.CurrentIndex = ac.AudioSourceData.StartIndex;
							ac.AudioSourceData.PlayingCurrentIndex = false;

							for (uint32_t i = 0; i < ac.AudioSourceData.Playlist.size(); i++)
							{
								Ref<AudioSource> audioSource = GetOrCreateRuntimePlaylistSource({ entity, this }, i, ac.AudioSourceData.Playlist[i]);

								if (audioSource != nullptr && audioSource->IsPlaying())
									audioSource->Stop();
							}
						}
					}
				});
		}
		ReleaseAllRuntimeAudio();

		m_Registry.view<NativeScriptComponent>().each([](auto, auto& nsc)
			{
				if (nsc.Instance)
				{
					if (nsc.DestroyScript)
						nsc.DestroyScript(&nsc);
					else
						delete nsc.Instance;  // Fallback delete if DestroyScript is null
					nsc.Instance = nullptr;
				}
			});

		m_Registry.view<AudioListenerComponent>().each([](auto, auto& alc)
			{
				alc.Listener.reset();
			});

		// Scripting: fire OnDestroy and tear down live managed instances (values stay in storage).
		{
			ScriptEngine& scriptEngine = ScriptEngine::GetMutable();
			for (auto& [entityID, instance] : m_ScriptInstances)
			{
				auto it = m_ScriptStorage.EntityStorage.find(entityID);
				if (it == m_ScriptStorage.EntityStorage.end())
					continue;

				if (scriptEngine.IsValidScript(it->second.ScriptID) &&
					scriptEngine.GetScriptMetadata(it->second.ScriptID).HasMethod("OnDestroy"))
					instance.Invoke("OnDestroy");

				scriptEngine.DestroyInstance(entityID, m_ScriptStorage);
			}
			m_ScriptInstances.clear();
			scriptEngine.SetCurrentScene(nullptr);
		}
	}

	void Scene::OnSimulationStart()
	{
		OnPhysics2DStart();
		OnPhysics3DStart();
	}

	void Scene::OnSimulationStop()
	{
		OnPhysics3DStop();
		OnPhysics2DStop();
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			// Update scripts
			{
				ScriptEngine& scriptEngine = ScriptEngine::GetMutable();
				auto view = m_Registry.view<ScriptComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					UUID entityID = entity.GetUUID();
					auto it = m_ScriptInstances.find(entityID);
					if (it == m_ScriptInstances.end())
						continue;

					const auto& sc = entity.GetComponent<ScriptComponent>();
					if (scriptEngine.GetScriptMetadata(sc.ScriptID).HasMethod("OnUpdate"))
						it->second.Invoke("OnUpdate", (float)ts);
				}

				m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
					{
						if (!nsc.Instance)
						{
							if (!nsc.InstantiateScript)
								return;

							nsc.Instance = nsc.InstantiateScript();
							if (!nsc.Instance)
								return;

							nsc.Instance->m_Entity = Entity{ entity, this };
							nsc.Instance->OnCreate();
						}

						nsc.Instance->OnUpdate(ts);
					});
			}

			StepPhysics(ts);

			{
				LUX_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioListenerComponent Scope", 0xFF7200);

				auto view = m_Registry.view<AudioListenerComponent>();
				view.each([&](entt::entity entity, AudioListenerComponent& alc)
					{
						Entity e = { entity, this };
						auto& ac = e.GetComponent<AudioListenerComponent>();

						if (ac.Active)
						{
							const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(e);
							const glm::mat4 inverted = glm::inverse(worldTransform);
							const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
							const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));
							ac.Listener->SetPosition(glm::vec4(worldPosition, 1.0f));
							ac.Listener->SetDirection(glm::vec3{ -forward.x, -forward.y, -forward.z });
							//break;
						}
					});
			}

			{
				LUX_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent Scope", 0xFF7200);

				auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
				view.each([&](entt::entity entityHandle, TransformComponent&, AudioSourceComponent& asc)
					{
						Entity entity = { entityHandle, this };
						const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(entity);
						const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);

						if (asc.Audio && !asc.AudioSourceData.UsePlaylist)
						{
							Ref<AudioSource> audioSource = GetOrCreateRuntimeAudioSource(entity, asc.Audio);
							if (!audioSource)
								return;

							if (!audioSource->IsPlaying() && asc.Paused)
							{
								audioSource->SetConfig(asc.Config);
								audioSource->Play();
								asc.Paused = false;
							}

							audioSource->SetConfig(asc.Config);
							audioSource->SetPosition(glm::vec4(worldPosition, 1.0f));
						}
						else if (asc.Audio && asc.AudioSourceData.UsePlaylist)
						{
							auto& playlist = asc.AudioSourceData.Playlist;

							if (playlist.empty())
								return;

							if (asc.AudioSourceData.OldIndex >= playlist.size())
								asc.AudioSourceData.OldIndex = 0;

							if (asc.AudioSourceData.CurrentIndex >= playlist.size())
							{
								if (asc.AudioSourceData.RepeatPlaylist)
									asc.AudioSourceData.CurrentIndex = 0;
								else
									return;
							}

							Ref<AudioSource> oldSource = GetOrCreateRuntimePlaylistSource(entity, asc.AudioSourceData.OldIndex, playlist[asc.AudioSourceData.OldIndex]);
							Ref<AudioSource> currentSource = GetOrCreateRuntimePlaylistSource(entity, asc.AudioSourceData.CurrentIndex, playlist[asc.AudioSourceData.CurrentIndex]);

							if (!currentSource)
								return;

							if (asc.Config.PlayOnAwake && !asc.Paused && (!oldSource || !oldSource->IsPlaying()))
							{
								if (!currentSource->IsLooping())
								{
									currentSource->SetConfig(asc.Config);
									currentSource->Play();
									currentSource->SetPosition(glm::vec4(worldPosition, 1.0f));

									asc.AudioSourceData.PlayingCurrentIndex = true;
									asc.Paused = false;
									asc.AudioSourceData.OldIndex = asc.AudioSourceData.CurrentIndex;
									asc.AudioSourceData.CurrentIndex++;
								}
							}
							else if (asc.Config.PlayOnAwake && asc.Paused)
							{
								currentSource->SetConfig(asc.Config);
								currentSource->Play();
								asc.AudioSourceData.PlayingCurrentIndex = true;
								asc.Paused = false;
							}
						}
					});
			}
		}
		else if (m_IsPaused)
		{
			LUX_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioListenerComponent 2 Scope", 0xFF7200);

			auto view = m_Registry.view<AudioListenerComponent>();
			view.each([&](entt::entity acEntity, AudioListenerComponent& alc)
				{
					Entity e = { acEntity, this };
					auto& ac = e.GetComponent<AudioListenerComponent>();

					if (ac.Active)
					{
						const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(e);
						const glm::mat4 inverted = glm::inverse(worldTransform);
						const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
						const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));
						ac.Listener->SetPosition(glm::vec4(worldPosition, 1.0f));
						ac.Listener->SetDirection(glm::vec3{ -forward.x, -forward.y, -forward.z });
					}
				});


			{
				LUX_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent 2 Scope", 0xFF7200);

				auto view = m_Registry.view<AudioSourceComponent>();
				view.each([&](entt::entity entity, AudioSourceComponent& asc)
					{

						Entity e = { entity , this };

						if (asc.Audio)
						{
							if (!asc.AudioSourceData.UsePlaylist)
							{
								Ref<AudioSource> audioSource = GetOrCreateRuntimeAudioSource(e, asc.Audio);
								if (audioSource && audioSource->IsPlaying())
								{
									audioSource->SetConfig(asc.Config);
									audioSource->Pause();
									asc.Paused = true;
								}
							}
							else if (asc.AudioSourceData.UsePlaylist)
							{
								if (asc.AudioSourceData.OldIndex == 0)
								{
									Ref<AudioSource> audioSourceIndex = GetOrCreateRuntimeAudioSource(e, asc.Audio);

									if (audioSourceIndex && audioSourceIndex->IsPlaying())
									{
										audioSourceIndex->SetConfig(asc.Config);
										audioSourceIndex->Pause();
										//ac.AudioSourceData.PlayingCurrentIndex = false;
										asc.Paused = true;
									}
								}
								else if (asc.AudioSourceData.OldIndex > 0)
								{
									if (asc.AudioSourceData.OldIndex < asc.AudioSourceData.Playlist.size())
									{
										Ref<AudioSource> audioSourceIndex = GetOrCreateRuntimePlaylistSource(e, asc.AudioSourceData.OldIndex, asc.AudioSourceData.Playlist[asc.AudioSourceData.OldIndex]);
										if (audioSourceIndex && audioSourceIndex->IsPlaying())
										{
											audioSourceIndex->SetConfig(asc.Config);
											audioSourceIndex->Pause();
											//ac.AudioSourceData.PlayingCurrentIndex = false;
											asc.Paused = true;
										}
									}
								}
							}
						}
					});
			}
		}

		// Render 2D
		Camera* mainCamera = nullptr;
		glm::mat4 cameraTransform;
		{
			auto view = m_Registry.view<TransformComponent, CameraComponent>();
			for (auto entity : view)
			{
				auto& camera = view.get<CameraComponent>(entity);
				if (camera.Primary)

				{
					mainCamera = &camera.Camera;
					cameraTransform = GetWorldSpaceTransformMatrix(Entity{ entity, this });
					break;
				}
			}
		}

		if (mainCamera)
		{
			glm::mat4 view = glm::inverse(cameraTransform);
			m_Renderer2D->BeginScene(mainCamera->GetProjectionMatrix() * view, view);

			// Draw sprites
			{
				auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
				for (auto entity : group)
				{
					auto& sprite = group.get<SpriteRendererComponent>(entity);

					Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(sprite.Texture);
					const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(Entity{ entity, this });
					if (texture)
						m_Renderer2D->DrawQuad(worldTransform, texture, sprite.TilingFactor, sprite.Color);
					else
						m_Renderer2D->DrawQuad(worldTransform, sprite.Color); // fallback to solid color
				}
			}

			// Draw circles
			{
				auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
				for (auto entity : view)
				{
					auto& circle = view.get<CircleRendererComponent>(entity);

					m_Renderer2D->DrawCircle(GetWorldSpaceTransformMatrix(Entity{ entity, this }), circle.Color);
				}
			}

			// Draw text
			{
				auto view = m_Registry.view<TransformComponent, TextComponent>();
				for (auto entity : view)
				{
					auto& text = view.get<TextComponent>(entity);

					Ref<Font> font = Font::GetFontAssetForTextComponent(text);
					if (font)
					{
						m_Renderer2D->DrawString(text.TextString, font, GetWorldSpaceTransformMatrix(Entity{ entity, this }), text.MaxWidth, text.Color, text.LineSpacing, text.Kerning);
					}
				}
			}

			m_Renderer2D->EndScene();
		}

		for (auto& func : m_PostUpdateQueue)
			func();
		m_PostUpdateQueue.clear();
	}

	void Scene::OnUpdateSimulation(Timestep ts, EditorCamera& camera)
	{
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			StepPhysics(ts);
		}

		// Render
		RenderScene(camera);

		for (auto& func : m_PostUpdateQueue)
			func();
		m_PostUpdateQueue.clear();
	}

	void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
	{
		// Render 2D
		RenderScene(camera);

		for (auto& func : m_PostUpdateQueue)
			func();
		m_PostUpdateQueue.clear();
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		if (m_ViewportWidth == width && m_ViewportHeight == height)
			return;

		m_ViewportWidth = width;
		m_ViewportHeight = height;

		// Resize our non-FixedAspectRatio cameras
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			auto& cameraComponent = view.get<CameraComponent>(entity);
			if (!cameraComponent.FixedAspectRatio)
				cameraComponent.Camera.SetViewportSize(width, height);
		}

	}

	void Scene::SetTargetFramebuffer(Ref<Framebuffer> framebuffer)
	{
		m_Renderer2D->SetTargetFramebuffer(framebuffer);
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			const auto& camera = view.get<CameraComponent>(entity);
			if (camera.Primary)
				return Entity{ entity, this };
		}
		return {};
	}

	void Scene::Step(int frames)
	{
		m_StepFrames = frames;
	}

	bool Scene::HasScripts() const
	{
		auto view = m_Registry.view<const ScriptComponent>();
		return view.begin() != view.end();
	}

	Ref<PhysicsScene> Scene::GetPhysicsScene() const
	{
		return m_PhysicsScene;
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		using DuplicateComponents =
			ComponentGroup<TransformComponent, SpriteRendererComponent, CircleRendererComponent, CameraComponent, ScriptComponent,
			NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent,
			RigidBodyComponent, CharacterControllerComponent, CompoundColliderComponent, BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent, TextComponent,
			MeshComponent, MeshTagComponent, PrefabComponent, StaticMeshComponent, SubmeshComponent,
			DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent,
			FolderComponent>;

		if (!entity)
			return {};

		std::function<Entity(Entity, Entity)> duplicateHierarchy;
		duplicateHierarchy = [&](Entity source, Entity parent) -> Entity
		{
			Entity destination = CreateEntity(source.GetName());
			CopyComponentIfExists(DuplicateComponents{}, destination, source);

			// TagComponent isn't in DuplicateComponents (name is set above); carry the editor lock/label.
			const auto& srcTag = source.GetComponent<TagComponent>();
			auto& dstTag = destination.GetComponent<TagComponent>();
			dstTag.Locked = srcTag.Locked;
			dstTag.LabelColor = srcTag.LabelColor;

			if (parent)
				ParentEntity(destination, parent);

			for (UUID childID : source.Children())
			{
				Entity child = TryGetEntityWithUUID(childID);
				if (child)
					duplicateHierarchy(child, destination);
			}

			return destination;
		};

		return duplicateHierarchy(entity, {});
	}

	Entity Scene::Instantiate(Ref<Prefab> prefab, const glm::vec3* translation, const glm::vec3* rotation, const glm::vec3* scale)
	{
		return InstantiateChild(prefab, {}, translation, rotation, scale);
	}

	Entity Scene::InstantiateChild(Ref<Prefab> prefab, Entity parent, const glm::vec3* translation, const glm::vec3* rotation, const glm::vec3* scale)
	{
		if (!prefab || !prefab->m_Entity)
			return {};

		return CreatePrefabEntity(prefab->m_Entity, parent, translation, rotation, scale);
	}

	Entity Scene::InstantiatePrefab(Ref<Prefab> prefab)
	{
		return Instantiate(prefab);
	}

	Entity Scene::CreatePrefabEntity(Entity entity, Entity parent, const glm::vec3* translation, const glm::vec3* rotation, const glm::vec3* scale)
	{
		if (!entity)
			return {};

		using PrefabInstantiationComponents =
			ComponentGroup<TransformComponent, SpriteRendererComponent, CircleRendererComponent, CameraComponent, ScriptComponent,
			NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent,
			RigidBodyComponent, CharacterControllerComponent, CompoundColliderComponent, BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent, TextComponent,
			MeshComponent, MeshTagComponent, StaticMeshComponent, SubmeshComponent,
			DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent,
			FolderComponent>;

		std::function<Entity(Entity, Entity)> instantiateHierarchy;
		instantiateHierarchy = [&](Entity source, Entity destinationParent) -> Entity
			{
				Entity destination = CreateEntity(source.GetName());
				CopyComponentIfExists(PrefabInstantiationComponents{}, destination, source);

				auto& prefabComponent = destination.AddOrReplaceComponent<PrefabComponent>();
				prefabComponent.PrefabID = source.HasComponent<PrefabComponent>() ? source.GetComponent<PrefabComponent>().PrefabID : AssetHandle(0);
				prefabComponent.EntityID = source.GetUUID();

				if (destinationParent)
					ParentEntity(destination, destinationParent);

				for (const UUID childID : source.Children())
				{
					Entity child = source.GetScene()->TryGetEntityWithUUID(childID);
					if (child)
						instantiateHierarchy(child, destination);
				}

				return destination;
			};

		Entity root = instantiateHierarchy(entity, parent);
		if (translation)
			root.Transform().Translation = *translation;
		if (rotation)
			root.Transform().SetRotationEuler(*rotation);
		if (scale)
			root.Transform().Scale = *scale;
		return root;
	}

	void Scene::CopyPrefabInstanceComponents(Entity destination, Entity source)
	{
		if (!destination || !source)
			return;

		// Same set the instantiation path syncs (identity components — ID/Relationship/Tag/Prefab —
		// are deliberately excluded so the link and hierarchy survive a revert/apply).
		using PrefabSyncComponents =
			ComponentGroup<TransformComponent, SpriteRendererComponent, CircleRendererComponent, CameraComponent, ScriptComponent,
			NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent,
			RigidBodyComponent, CharacterControllerComponent, CompoundColliderComponent, BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent, TextComponent,
			MeshComponent, MeshTagComponent, StaticMeshComponent, SubmeshComponent,
			DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent,
			FolderComponent>;

		CopyComponentIfExists(PrefabSyncComponents{}, destination, source);
	}

	Entity Scene::InstantiateMesh(Ref<Mesh> mesh)
	{
		if (!mesh)
			return {};

		Entity entity = CreateEntity("Mesh");
		entity.AddComponent<MeshComponent>(mesh->Handle);
		return entity;
	}

	Entity Scene::InstantiateStaticMesh(Ref<StaticMesh> mesh)
	{
		if (!mesh)
			return {};

		Entity entity = CreateEntity("Static Mesh");
		entity.AddComponent<StaticMeshComponent>(mesh->Handle);
		return entity;
	}

	Entity Scene::FindEntityByName(std::string_view name)
	{
		auto view = m_Registry.view<TagComponent>();
		for (auto entity : view)
		{
			const TagComponent& tc = view.get<TagComponent>(entity);
			if (tc.Tag == name)
				return Entity{ entity, this };
		}
		return {};
	}

	Entity Scene::GetEntityByUUID(UUID uuid) const
	{
		if (m_EntityMap.find(uuid) != m_EntityMap.end())
			return { m_EntityMap.at(uuid), const_cast<Scene*>(this) };

		return {};
	}

	Entity Scene::GetEntityWithUUID(UUID uuid) const
	{
		Entity entity = TryGetEntityWithUUID(uuid);
		LUX_CORE_ASSERT(entity, "Entity does not exist!");
		return entity;
	}

	Entity Scene::TryGetEntityWithUUID(UUID uuid) const
	{
		return GetEntityByUUID(uuid);
	}

	Entity Scene::TryGetEntityWithTag(const std::string& tag)
	{
		auto view = m_Registry.view<TagComponent>();
		for (auto entity : view)
		{
			if (view.get<TagComponent>(entity).Tag == tag)
				return { entity, this };
		}
		return {};
	}

	glm::mat4 Scene::GetWorldSpaceTransformMatrix(Entity entity) const
	{
		if (!entity || !entity.HasComponent<TransformComponent>())
			return glm::mat4(1.0f);

		glm::mat4 transform = entity.GetComponent<TransformComponent>().GetTransform();

		if (entity.HasComponent<RelationshipComponent>())
		{
			const auto& relationship = entity.GetComponent<RelationshipComponent>();
			if (relationship.ParentHandle != 0)
			{
				Entity parent = GetEntityByUUID(relationship.ParentHandle);
				if (parent)
					transform = GetWorldSpaceTransformMatrix(parent) * transform;
			}
		}

		return transform;
	}

	TransformComponent Scene::GetWorldSpaceTransform(Entity entity) const
	{
		TransformComponent transform;
		transform.SetTransform(GetWorldSpaceTransformMatrix(entity));
		return transform;
	}

	void Scene::ConvertToLocalSpace(Entity entity)
	{
		Entity parent = entity.GetParent();
		if (!parent)
			return;

		const glm::mat4 parentTransform = GetWorldSpaceTransformMatrix(parent);
		const glm::mat4 localTransform = glm::inverse(parentTransform) * GetWorldSpaceTransformMatrix(entity);
		entity.Transform().SetTransform(localTransform);
	}

	void Scene::ConvertToWorldSpace(Entity entity)
	{
		entity.Transform().SetTransform(GetWorldSpaceTransformMatrix(entity));
	}

	void Scene::ParentEntity(Entity entity, Entity parent)
	{
		if (!entity || !parent || entity == parent || parent.IsDescendantOf(entity))
			return;

		const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(entity);
		entity.SetParent(parent);
		entity.Transform().SetTransform(glm::inverse(GetWorldSpaceTransformMatrix(parent)) * worldTransform);
		SortEntities();
	}

	void Scene::UnparentEntity(Entity entity, bool convertToWorldSpace)
	{
		if (!entity)
			return;

		glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(entity);
		entity.SetParent({});
		if (convertToWorldSpace)
			entity.Transform().SetTransform(worldTransform);
		SortEntities();
	}

	std::vector<UUID> Scene::GetAllChildren(Entity entity) const
	{
		std::vector<UUID> result;
		if (!entity || !entity.HasComponent<RelationshipComponent>())
			return result;

		for (UUID childID : entity.Children())
		{
			result.emplace_back(childID);
			Entity child = TryGetEntityWithUUID(childID);
			if (!child)
				continue;

			std::vector<UUID> grandchildren = GetAllChildren(child);
			result.insert(result.end(), grandchildren.begin(), grandchildren.end());
		}

		return result;
	}

	void Scene::CopyTo(Ref<Scene>& target)
	{
		if (!target)
			target = Ref<Scene>::Create();

		target->m_Registry.clear();
		target->m_EntityMap.clear();
		target->m_Name = m_Name;
		target->m_ViewportWidth = m_ViewportWidth;
		target->m_ViewportHeight = m_ViewportHeight;

		std::unordered_map<UUID, entt::entity> enttMap;
		auto idView = m_Registry.view<IDComponent>();
		for (auto entity : idView)
		{
			UUID uuid = m_Registry.get<IDComponent>(entity).ID;
			const auto& name = m_Registry.get<TagComponent>(entity).Tag;
			Entity newEntity = target->CreateEntityWithID(uuid, name, false);
			enttMap[uuid] = (entt::entity)newEntity;
		}

		CopyComponent(AllComponents{}, target->m_Registry, m_Registry, enttMap);
		target->SortEntities();
	}

	void Scene::SortEntities()
	{
		m_Registry.sort<IDComponent>([](const entt::entity lhs, const entt::entity rhs)
		{
			return (uint32_t)lhs < (uint32_t)rhs;
		});
	}

	void Scene::OnPhysics2DStart()
	{
		m_PhysicsScene2D = CreateScope<PhysicsScene2D>(this);
		m_PhysicsScene2D->Start();
	}

	void Scene::OnPhysics2DStop()
	{
		if (m_PhysicsScene2D)
		{
			m_PhysicsScene2D->Stop();
			m_PhysicsScene2D.reset();
		}
	}

	void Scene::OnPhysics3DStart()
	{
		m_PhysicsScene = PhysicsSystem::CreateScene(this);
		if (m_PhysicsScene)
			m_PhysicsScene->Start();
	}

	void Scene::OnPhysics3DStop()
	{
		if (m_PhysicsScene)
		{
			m_PhysicsScene->Stop();
			m_PhysicsScene.reset();
		}
	}

	void Scene::StepPhysics(Timestep ts)
	{
		if (m_PhysicsScene)
			m_PhysicsScene->Simulate(ts);

		if (m_PhysicsScene2D)
			m_PhysicsScene2D->Simulate(ts);
	}

	void Scene::RenderScene(EditorCamera& camera)
	{
		m_Renderer2D->BeginScene(camera.GetViewProjection(), camera.GetViewMatrix());

		// Draw sprites
		{
			auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
			for (auto entity : group)
			{
				auto& sprite = group.get<SpriteRendererComponent>(entity);
				Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(sprite.Texture);
				const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(Entity{ entity, this });
				if (texture)
					m_Renderer2D->DrawQuad(worldTransform, texture, sprite.TilingFactor, sprite.Color);
				else
					m_Renderer2D->DrawQuad(worldTransform, sprite.Color);
			}
		}

		// Draw circles
		{
			auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
			for (auto entity : view)
			{
				auto& circle = view.get<CircleRendererComponent>(entity);
				m_Renderer2D->DrawCircle(GetWorldSpaceTransformMatrix(Entity{ entity, this }), circle.Color);
			}
		}
		// Draw text
		{
			auto view = m_Registry.view<TransformComponent, TextComponent>();
			for (auto entity : view)
			{
				auto& text = view.get<TextComponent>(entity);
				Ref<Font> font = Font::GetFontAssetForTextComponent(text);
				if (font)
				{
					m_Renderer2D->DrawString(text.TextString, font, GetWorldSpaceTransformMatrix(Entity{ entity, this }), text.MaxWidth, text.Color, text.LineSpacing, text.Kerning);
				}
			}
		}

		m_Renderer2D->EndScene();
	}

	// ============================================================================
	// 3D Rendering Support
	// ============================================================================

	LightEnvironment Scene::CollectLightEnvironment() const
	{
		LightEnvironment lightEnv;

		// Collect directional lights
		{
			auto view = m_Registry.view<const TransformComponent, const DirectionalLightComponent>();
			uint32_t dirLightIndex = 0;
			for (auto entity : view)
			{
				if (dirLightIndex >= LightEnvironment::MaxDirectionalLights)
					break;

				const auto& dirLight = view.get<const DirectionalLightComponent>(entity);
				const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(Entity{ entity, const_cast<Scene*>(this) });

				glm::vec3 direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

				lightEnv.DirectionalLights[dirLightIndex].Direction = direction;
				lightEnv.DirectionalLights[dirLightIndex].Radiance = PhysicalLight::EffectiveRadiance(dirLight.Radiance, dirLight.UseColorTemperature, dirLight.ColorTemperature);
				lightEnv.DirectionalLights[dirLightIndex].Intensity = PhysicalLight::DirectionalIntensity(dirLight.Unit, dirLight.Intensity);
				lightEnv.DirectionalLights[dirLightIndex].ShadowAmount = dirLight.ShadowAmount;
				lightEnv.DirectionalLights[dirLightIndex].CastShadows = dirLight.CastShadows;
				lightEnv.DirectionalLights[dirLightIndex].SoftShadows = dirLight.SoftShadows;
				lightEnv.DirectionalLights[dirLightIndex].LightSize = dirLight.LightSize;
				lightEnv.DirectionalLights[dirLightIndex].ShadowDistance = dirLight.ShadowDistance;
				lightEnv.DirectionalLights[dirLightIndex].ShadowResolutionTier = dirLight.ShadowResolutionTier;

				dirLightIndex++;
			}
		}

		// Collect point lights
		{
			auto view = m_Registry.view<const TransformComponent, const PointLightComponent>();
			for (auto entity : view)
			{
				const auto& pointLight = view.get<const PointLightComponent>(entity);
				const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(Entity{ entity, const_cast<Scene*>(this) });

				PointLight pl;
				pl.Position = glm::vec3(worldTransform[3]);
				pl.Radiance = PhysicalLight::EffectiveRadiance(pointLight.Radiance, pointLight.UseColorTemperature, pointLight.ColorTemperature);
				pl.Intensity = PhysicalLight::PointIntensity(pointLight.Unit, pointLight.Intensity);
				pl.Radius = pointLight.Radius;
				pl.Falloff = pointLight.Falloff;
				pl.MinRadius = pointLight.MinRadius;
				pl.LightSize = pointLight.LightSize;
				pl.CastsShadows = pointLight.CastsShadows ? 1u : 0u;

				lightEnv.PointLights.push_back(pl);
			}
		}

		// Collect spot lights
		{
			auto view = m_Registry.view<const TransformComponent, const SpotLightComponent>();
			for (auto entity : view)
			{
				const auto& spotLight = view.get<const SpotLightComponent>(entity);
				const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(Entity{ entity, const_cast<Scene*>(this) });

				SpotLight sl;
				sl.Position = glm::vec3(worldTransform[3]);
				sl.Direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
				sl.Radiance = PhysicalLight::EffectiveRadiance(spotLight.Radiance, spotLight.UseColorTemperature, spotLight.ColorTemperature);
				sl.Intensity = PhysicalLight::SpotIntensity(spotLight.Unit, spotLight.Intensity, spotLight.Angle);
				sl.Range = spotLight.Range;
				sl.Angle = spotLight.Angle;
				sl.AngleAttenuation = spotLight.AngleAttenuation;
				sl.Falloff = spotLight.Falloff;
				sl.SoftShadows = spotLight.SoftShadows ? 1u : 0u;
				sl.CastsShadows = spotLight.CastsShadows ? 1u : 0u;
				sl.ShadowDistance = spotLight.ShadowDistance;
				sl.ShadowResolutionTier = spotLight.ShadowResolutionTier;

				lightEnv.SpotLights.push_back(sl);
			}
		}

		return lightEnv;
	}

	Ref<Environment> Scene::CollectEnvironment(float& outIntensity, float& outLod) const
	{
		outIntensity = 1.0f;
		outLod = 0.0f;
		Ref<Environment> fallbackEnvironment = Renderer::GetDefaultEnvironment();
		if (!fallbackEnvironment)
			fallbackEnvironment = Renderer::GetEmptyEnvironment();

		auto view = m_Registry.view<const SkyLightComponent>();
		for (auto entity : view)
		{
			const auto& skyLight = view.get<const SkyLightComponent>(entity);
			outIntensity = skyLight.Intensity;
			outLod = skyLight.Lod;

			if (skyLight.DynamicSky)
			{
				const glm::vec3 parameters = skyLight.TurbidityAzimuthInclination;
				const bool needsRebuild = !m_DynamicSkyEnvironmentValid
					|| !m_DynamicSkyEnvironment
					|| m_DynamicSkyParameters.x != parameters.x
					|| m_DynamicSkyParameters.y != parameters.y
					|| m_DynamicSkyParameters.z != parameters.z;

				if (needsRebuild)
				{
					// Keep a transient scene-local cache so we do not regenerate the sky every frame.
					m_DynamicSkyEnvironment = Renderer::CreatePreethamSkyEnvironment(parameters.x, parameters.y, parameters.z);
					m_DynamicSkyParameters = parameters;
					m_DynamicSkyEnvironmentValid = m_DynamicSkyEnvironment
						&& m_DynamicSkyEnvironment->RadianceMap
						&& m_DynamicSkyEnvironment->IrradianceMap;
				}

				if (m_DynamicSkyEnvironmentValid)
					return m_DynamicSkyEnvironment;
			}

			if (skyLight.SceneEnvironment)
			{
				Ref<Asset> asset = AssetManager::GetAsset<Asset>(skyLight.SceneEnvironment);
				if (asset && asset->GetAssetType() != AssetType::EnvMap)
				{
					AssetManager::ReloadData(skyLight.SceneEnvironment);
					asset = AssetManager::GetAsset<Asset>(skyLight.SceneEnvironment);
				}

				if (!asset || asset->GetAssetType() != AssetType::EnvMap)
					continue;

				Ref<Environment> environment = asset.As<Environment>();
				if (environment && environment->RadianceMap && environment->IrradianceMap)
					return environment;
			}

			return fallbackEnvironment;
		}

		return fallbackEnvironment;
	}

	Ref<::Lux::RenderScene> Scene::SyncRenderScene(const std::function<bool(Entity)>& isSelected) const
	{
		struct StaticMeshSyncItem
		{
			StaticMeshRenderProxy Proxy;
			Entity EntityHandle;
			bool HasParent = false;
		};

		if (!m_RenderScene)
			m_RenderScene = Ref<::Lux::RenderScene>::Create();

		m_RenderScene->BeginSync();

		// Per-call scratch: thread_local (not a Scene member) because Scene.h only
		// forward-declares the render types. Cleared at both ends of the call so no
		// Ref<>s outlive it — only raw capacity is retained across frames.
		static thread_local std::vector<StaticMeshSyncItem> syncItems;
		syncItems.clear();
		auto view = m_Registry.view<const TransformComponent, const StaticMeshComponent>();
		for (auto e : view)
		{
			Entity entity = { e, const_cast<Scene*>(this) };
			const UUID entityID = entity.GetUUID();
			const auto& meshComp = view.get<const StaticMeshComponent>(e);

			if (!meshComp.StaticMesh)
				continue;

			Ref<StaticMesh> staticMesh;
			Ref<MeshSource> meshSource;
			AssetHandle meshSourceHandle = 0;

			const StaticMeshRenderProxy* previousProxy = m_RenderScene->FindStaticMeshProxy(entityID);
			if (previousProxy && previousProxy->StaticMeshHandle == meshComp.StaticMesh && previousProxy->StaticMesh && previousProxy->MeshSource)
			{
				staticMesh = previousProxy->StaticMesh;
				meshSource = previousProxy->MeshSource;
				meshSourceHandle = previousProxy->MeshSourceHandle;
			}
			else
			{
				staticMesh = StaticMesh::GetOrCreateRuntime(meshComp.StaticMesh);
				if (!staticMesh)
					continue;

				meshSourceHandle = staticMesh->GetMeshSource();
				meshSource = AssetManager::GetAsset<MeshSource>(meshSourceHandle);
				if (!meshSource)
					continue;
			}

			Ref<MaterialTable> materialTable = meshComp.MaterialTable && !meshComp.MaterialTable->GetMaterials().empty()
				? meshComp.MaterialTable
				: staticMesh->GetMaterials();

			StaticMeshSyncItem& syncItem = syncItems.emplace_back();
			syncItem.Proxy.EntityID = entityID;
			syncItem.Proxy.StaticMeshHandle = meshComp.StaticMesh;
			syncItem.Proxy.MeshSourceHandle = meshSourceHandle;
			syncItem.Proxy.StaticMesh = staticMesh;
			syncItem.Proxy.MeshSource = meshSource;
			syncItem.Proxy.MaterialTable = materialTable;
			syncItem.Proxy.Visible = meshComp.Visible;
			syncItem.Proxy.Selected = isSelected ? isSelected(entity) : false;

			// Defer world-transform computation to the parallel pass below (it's the expensive,
			// write-disjoint part). Just record the entity and whether it needs a hierarchy walk.
			syncItem.EntityHandle = entity;
			syncItem.HasParent = entity.HasComponent<RelationshipComponent>()
				&& entity.GetComponent<RelationshipComponent>().ParentHandle != 0;
		}

		auto computeItem = [&](size_t index)
			{
				StaticMeshSyncItem& syncItem = syncItems[index];

				// Parented entities walk the relationship hierarchy (GetWorldSpaceTransformMatrix is
				// read-only); others just decompose their local transform. SyncRenderScene is const and
				// runs with no concurrent registry writes, so these registry reads are safe across threads.
				if (syncItem.HasParent)
					syncItem.Proxy.WorldTransform = GetWorldSpaceTransformMatrix(syncItem.EntityHandle);
				else
					syncItem.Proxy.WorldTransform = syncItem.EntityHandle.GetComponent<TransformComponent>().GetTransform();

				const BoundingSphere localBounds = syncItem.Proxy.MeshSource->GetBoundingBox().ToBoundingSphere();
				syncItem.Proxy.WorldBounds = BoundingSphere::Transform(localBounds, syncItem.Proxy.WorldTransform);
			};

		// Fan out the per-proxy transform/bounds math across the job pool. Each index writes only its
		// own syncItem, so the indices are disjoint and safe to process concurrently. The minimum chunk
		// keeps small scenes on the calling thread (JobSystem runs inline when single-threaded anyway).
		constexpr size_t parallelTransformThreshold = 512;
		JobSystem::ParallelFor(syncItems.size(), computeItem, parallelTransformThreshold);

		for (StaticMeshSyncItem& syncItem : syncItems)
			m_RenderScene->UpsertStaticMesh(std::move(syncItem.Proxy));

		// Release the Ref<>s now rather than at thread_local destruction, which
		// would race engine shutdown (asset manager teardown, LUX_TRACK_MEMORY).
		syncItems.clear();

		m_RenderScene->EndSync();
		return m_RenderScene;
	}

	void Scene::SubmitStaticMeshes(Ref<SceneRenderer> renderer,
		const std::function<bool(Entity)>& isSelected) const
	{
		renderer->SubmitRenderScene(SyncRenderScene(isSelected));

		const SceneRendererOptions& rendererOptions = renderer->GetOptions();
		if (!rendererOptions.ShowPhysicsColliders)
			return;

		auto shouldSubmitCollider = [&](Entity entity)
		{
			if (rendererOptions.PhysicsColliderMode == SceneRendererOptions::PhysicsColliderView::All)
				return true;

			return isSelected ? isSelected(entity) : false;
		};

		auto submitDebugMesh = [&](AssetHandle handle, const glm::mat4& transform, bool isSimpleCollider)
		{
			Ref<StaticMesh> staticMesh;
			Ref<MeshSource> meshSource;
			if (!ResolveStaticMeshDebugAssets(handle, staticMesh, meshSource))
				return;

			renderer->SubmitPhysicsStaticDebugMesh(staticMesh, meshSource, transform, isSimpleCollider);
		};

		{
			auto colliderView = m_Registry.view<const TransformComponent, const BoxColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const BoxColliderComponent>(e);
				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const glm::vec3 size = glm::max(collider.HalfSize * 2.0f * physicsScale, glm::vec3(0.001f));
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::translate(glm::mat4(1.0f), collider.Offset * physicsScale)
					* glm::scale(glm::mat4(1.0f), size);

				submitDebugMesh(GetColliderDebugPrimitiveMesh(ColliderDebugPrimitive::Box), transform, true);
			}
		}

		{
			auto colliderView = m_Registry.view<const TransformComponent, const SphereColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const SphereColliderComponent>(e);
				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const float radius = std::max(0.001f, collider.Radius * std::max({ physicsScale.x, physicsScale.y, physicsScale.z }));
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::translate(glm::mat4(1.0f), collider.Offset * physicsScale)
					* glm::scale(glm::mat4(1.0f), glm::vec3(radius));

				submitDebugMesh(GetColliderDebugPrimitiveMesh(ColliderDebugPrimitive::Sphere), transform, true);
			}
		}

		{
			auto colliderView = m_Registry.view<const TransformComponent, const CapsuleColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const CapsuleColliderComponent>(e);
				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const float radius = std::max(0.001f, collider.Radius * std::max(physicsScale.x, physicsScale.z));
				const float halfHeight = std::max(0.001f, collider.HalfHeight * physicsScale.y);
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::translate(glm::mat4(1.0f), collider.Offset * physicsScale);

				submitDebugMesh(GetCapsuleColliderDebugMesh(radius, halfHeight * 2.0f), transform, true);
			}
		}

		{
			auto controllerView = m_Registry.view<const TransformComponent, const CharacterControllerComponent>();
			for (auto e : controllerView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity) || entity.HasComponent<CapsuleColliderComponent>())
					continue;

				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const float radius = 0.5f * std::max(physicsScale.x, physicsScale.z);
				const float halfHeight = 0.5f * physicsScale.y;
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform);

				submitDebugMesh(GetCapsuleColliderDebugMesh(radius, halfHeight * 2.0f), transform, true);
			}
		}

		{
			auto colliderView = m_Registry.view<const TransformComponent, const MeshColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const MeshColliderComponent>(e);
				const AssetHandle colliderHandle = ResolveMeshColliderHandle(entity, collider);
				if (!colliderHandle)
					continue;

				Ref<StaticMesh> staticMesh;
				Ref<MeshSource> meshSource;
				if (!ResolveStaticMeshDebugAssets(colliderHandle, staticMesh, meshSource))
					continue;

				if (collider.SubmeshIndex < meshSource->GetSubmeshes().size())
					staticMesh = Ref<StaticMesh>::Create(staticMesh->GetMeshSource(), std::vector<uint32_t>{ collider.SubmeshIndex }, false);

				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::scale(glm::mat4(1.0f), physicsScale);

				renderer->SubmitPhysicsStaticDebugMesh(staticMesh, meshSource, transform, false);
			}
		}
	}

	void Scene::Render3D(const EditorCamera& camera, Ref<SceneRenderer> renderer,
		const std::function<bool(Entity)>& isSelected)
	{
		if (!renderer || !renderer->IsReady())
			return;

		FrameRenderPacket packet;
		{
			LUX_PROFILE_SCOPE("Scene::BuildRenderPacket");
			BuildRenderPacketEditor(packet, camera, renderer, isSelected);
		}
		SubmitRenderPacket(renderer, packet);
	}

	void Scene::BuildRenderPacketEditor(FrameRenderPacket& packet, const EditorCamera& camera,
		Ref<SceneRenderer> renderer, const std::function<bool(Entity)>& isSelected)
	{
		// Set up the SceneRendererCamera from the EditorCamera
		SceneRendererCamera& sceneCamera = packet.Camera;
		sceneCamera.Camera.SetProjectionMatrix(camera.GetProjectionMatrix(), camera.GetUnReversedProjectionMatrix());
		sceneCamera.ViewMatrix = camera.GetViewMatrix();
		sceneCamera.Near = camera.GetNearClip();
		sceneCamera.Far = camera.GetFarClip();
		sceneCamera.FOV = camera.GetVerticalFOV();

		packet.Overlay2DView = camera.GetViewMatrix();
		packet.Overlay2DViewProjection = camera.GetViewProjection();

		// Collect light / environment / volumes (all read-only on the registry)
		packet.Lights = CollectLightEnvironment();
		packet.SkyEnvironment = CollectEnvironment(packet.EnvironmentIntensity, packet.EnvironmentLod);
		packet.PostProcess = m_PostProcessSettings;

		// Mesh proxies, 2D overlay items and physics-collider debug meshes
		packet.Meshes = SyncRenderScene(isSelected);
		CaptureDraw2D(packet);
		CaptureColliderDebug(packet, renderer, isSelected);

		packet.Valid = true;
	}

	void Scene::CaptureDraw2D(FrameRenderPacket& packet)
	{
		using Draw2DItem = FrameRenderPacket::Draw2DItem;

		// Draw sprites
		{
			auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
			for (auto entity : group)
			{
				auto& sprite = group.get<SpriteRendererComponent>(entity);

				Draw2DItem item;
				item.Transform = GetWorldSpaceTransformMatrix(Entity{ entity, this });
				item.Color = sprite.Color;
				item.Texture = AssetManager::GetAsset<Texture2D>(sprite.Texture);
				item.TilingFactor = sprite.TilingFactor;
				item.Type = item.Texture ? Draw2DItem::Kind::TexturedQuad : Draw2DItem::Kind::Quad;
				packet.Draw2D.push_back(std::move(item));
			}
		}

		// Draw circles
		{
			auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
			for (auto entity : view)
			{
				auto& circle = view.get<CircleRendererComponent>(entity);

				Draw2DItem item;
				item.Type = Draw2DItem::Kind::Circle;
				item.Transform = GetWorldSpaceTransformMatrix(Entity{ entity, const_cast<Scene*>(this) });
				item.Color = circle.Color;
				packet.Draw2D.push_back(std::move(item));
			}
		}

		// Draw text
		{
			auto view = m_Registry.view<TransformComponent, TextComponent>();
			for (auto entity : view)
			{
				auto& text = view.get<TextComponent>(entity);

				Ref<Font> font = Font::GetFontAssetForTextComponent(text);
				if (!font)
					continue;

				Draw2DItem item;
				item.Type = Draw2DItem::Kind::Text;
				item.Transform = GetWorldSpaceTransformMatrix(Entity{ entity, const_cast<Scene*>(this) });
				item.Color = text.Color;
				item.FontAsset = font;
				item.Text = text.TextString;
				item.MaxWidth = text.MaxWidth;
				item.LineSpacing = text.LineSpacing;
				item.Kerning = text.Kerning;
				packet.Draw2D.push_back(std::move(item));
			}
		}
	}

	void Scene::SubmitRenderPacket(Ref<SceneRenderer> renderer, const FrameRenderPacket& packet) const
	{
		LUX_PROFILE_FUNCTION("Scene::SubmitRenderPacket");
		if (!renderer || !renderer->IsReady() || !packet.Valid)
			return;

		renderer->SetLightEnvironment(packet.Lights);
		renderer->SetEnvironment(packet.SkyEnvironment, packet.EnvironmentIntensity, packet.EnvironmentLod);
		renderer->SetPostProcessSettings(packet.PostProcess);

		renderer->BeginScene(packet.Camera);

		{
			LUX_PROFILE_SCOPE("Scene::SubmitMeshes");
			renderer->SubmitRenderScene(packet.Meshes);
			for (const FrameRenderPacket::ColliderDebugItem& collider : packet.ColliderDebug)
				renderer->SubmitPhysicsStaticDebugMesh(collider.Mesh, collider.Source, collider.Transform, collider.SimpleCollider);
		}

		// The 2D overlay runs as a deferred render-graph pass (possibly off the submitting thread), so the
		// callback owns its own copy of the captured draw items - it never touches the live registry.
		std::vector<FrameRenderPacket::Draw2DItem> draw2D = packet.Draw2D;
		const glm::mat4 overlayView = packet.Overlay2DView;
		const glm::mat4 overlayViewProjection = packet.Overlay2DViewProjection;
		renderer->SetWorldOverlayRenderCallback([renderer, draw2D = std::move(draw2D), overlayView, overlayViewProjection]() mutable
		{
			Ref<Renderer2D> renderer2D = renderer->GetRenderer2D();
			if (!renderer2D)
				return;

			renderer2D->ResetStats();
			renderer2D->BeginScene(overlayViewProjection, overlayView, true);
			renderer2D->SetTargetFramebuffer(renderer->GetDepthCompositeFramebuffer());

			for (const FrameRenderPacket::Draw2DItem& item : draw2D)
			{
				switch (item.Type)
				{
					case FrameRenderPacket::Draw2DItem::Kind::TexturedQuad:
						renderer2D->DrawQuad(item.Transform, item.Texture, item.TilingFactor, item.Color);
						break;
					case FrameRenderPacket::Draw2DItem::Kind::Quad:
						renderer2D->DrawQuad(item.Transform, item.Color);
						break;
					case FrameRenderPacket::Draw2DItem::Kind::Circle:
						renderer2D->DrawCircle(item.Transform, item.Color);
						break;
					case FrameRenderPacket::Draw2DItem::Kind::Text:
						if (item.FontAsset)
							renderer2D->DrawString(item.Text, item.FontAsset, item.Transform, item.MaxWidth, item.Color, item.LineSpacing, item.Kerning);
						break;
				}
			}

			renderer2D->EndScene();
		});

		// End the frame and execute the render passes
		renderer->EndScene();
	}

	void Scene::Render3DRuntime(Ref<SceneRenderer> renderer)
	{
		if (!renderer || !renderer->IsReady())
			return;

		FrameRenderPacket packet;
		if (!BuildRenderPacketRuntime(packet, renderer))
			return;
		SubmitRenderPacket(renderer, packet);
	}

	bool Scene::BuildRenderPacketRuntime(FrameRenderPacket& packet, Ref<SceneRenderer> renderer)
	{
		// Find the primary camera entity
		Entity cameraEntity = GetPrimaryCameraEntity();
		if (!cameraEntity)
			return false;

		const auto& cameraComp = cameraEntity.GetComponent<CameraComponent>();

		// Set up the SceneRendererCamera from the runtime camera
		SceneRendererCamera& sceneCamera = packet.Camera;
		sceneCamera.Camera.SetProjectionMatrix(cameraComp.Camera.GetProjectionMatrix(), cameraComp.Camera.GetUnReversedProjectionMatrix());
		sceneCamera.ViewMatrix = glm::inverse(GetWorldSpaceTransformMatrix(cameraEntity));

		packet.Overlay2DView = sceneCamera.ViewMatrix;
		packet.Overlay2DViewProjection = cameraComp.Camera.GetProjectionMatrix() * sceneCamera.ViewMatrix;

		packet.Lights = CollectLightEnvironment();
		packet.SkyEnvironment = CollectEnvironment(packet.EnvironmentIntensity, packet.EnvironmentLod);
		packet.PostProcess = m_PostProcessSettings;

		// Submit all static meshes (no selection highlight in runtime)
		packet.Meshes = SyncRenderScene(nullptr);
		CaptureDraw2D(packet);
		CaptureColliderDebug(packet, renderer, nullptr);

		packet.Valid = true;
		return true;
	}

	void Scene::CaptureColliderDebug(FrameRenderPacket& packet, Ref<SceneRenderer> renderer,
		const std::function<bool(Entity)>& isSelected)
	{
		const SceneRendererOptions& rendererOptions = renderer->GetOptions();
		if (!rendererOptions.ShowPhysicsColliders)
			return;

		auto shouldSubmitCollider = [&](Entity entity)
		{
			if (rendererOptions.PhysicsColliderMode == SceneRendererOptions::PhysicsColliderView::All)
				return true;

			return isSelected ? isSelected(entity) : false;
		};

		auto pushDebugMesh = [&](AssetHandle handle, const glm::mat4& transform, bool isSimpleCollider)
		{
			Ref<StaticMesh> staticMesh;
			Ref<MeshSource> meshSource;
			if (!ResolveStaticMeshDebugAssets(handle, staticMesh, meshSource))
				return;

			FrameRenderPacket::ColliderDebugItem item;
			item.Mesh = staticMesh;
			item.Source = meshSource;
			item.Transform = transform;
			item.SimpleCollider = isSimpleCollider;
			packet.ColliderDebug.push_back(std::move(item));
		};

		{
			auto colliderView = m_Registry.view<const TransformComponent, const BoxColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const BoxColliderComponent>(e);
				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const glm::vec3 size = glm::max(collider.HalfSize * 2.0f * physicsScale, glm::vec3(0.001f));
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::translate(glm::mat4(1.0f), collider.Offset * physicsScale)
					* glm::scale(glm::mat4(1.0f), size);

				pushDebugMesh(GetColliderDebugPrimitiveMesh(ColliderDebugPrimitive::Box), transform, true);
			}
		}

		{
			auto colliderView = m_Registry.view<const TransformComponent, const SphereColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const SphereColliderComponent>(e);
				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const float radius = std::max(0.001f, collider.Radius * std::max({ physicsScale.x, physicsScale.y, physicsScale.z }));
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::translate(glm::mat4(1.0f), collider.Offset * physicsScale)
					* glm::scale(glm::mat4(1.0f), glm::vec3(radius));

				pushDebugMesh(GetColliderDebugPrimitiveMesh(ColliderDebugPrimitive::Sphere), transform, true);
			}
		}

		{
			auto colliderView = m_Registry.view<const TransformComponent, const CapsuleColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const CapsuleColliderComponent>(e);
				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const float radius = std::max(0.001f, collider.Radius * std::max(physicsScale.x, physicsScale.z));
				const float halfHeight = std::max(0.001f, collider.HalfHeight * physicsScale.y);
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::translate(glm::mat4(1.0f), collider.Offset * physicsScale);

				pushDebugMesh(GetCapsuleColliderDebugMesh(radius, halfHeight * 2.0f), transform, true);
			}
		}

		{
			auto controllerView = m_Registry.view<const TransformComponent, const CharacterControllerComponent>();
			for (auto e : controllerView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity) || entity.HasComponent<CapsuleColliderComponent>())
					continue;

				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const float radius = 0.5f * std::max(physicsScale.x, physicsScale.z);
				const float halfHeight = 0.5f * physicsScale.y;
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform);

				pushDebugMesh(GetCapsuleColliderDebugMesh(radius, halfHeight * 2.0f), transform, true);
			}
		}

		{
			auto colliderView = m_Registry.view<const TransformComponent, const MeshColliderComponent>();
			for (auto e : colliderView)
			{
				Entity entity = { e, const_cast<Scene*>(this) };
				if (!shouldSubmitCollider(entity))
					continue;

				const auto& collider = colliderView.get<const MeshColliderComponent>(e);
				const AssetHandle colliderHandle = ResolveMeshColliderHandle(entity, collider);
				if (!colliderHandle)
					continue;

				Ref<StaticMesh> staticMesh;
				Ref<MeshSource> meshSource;
				if (!ResolveStaticMeshDebugAssets(colliderHandle, staticMesh, meshSource))
					continue;

				if (collider.SubmeshIndex < meshSource->GetSubmeshes().size())
					staticMesh = Ref<StaticMesh>::Create(staticMesh->GetMeshSource(), std::vector<uint32_t>{ collider.SubmeshIndex }, false);

				const TransformComponent worldTransform = GetWorldSpaceTransform(entity);
				const glm::vec3 physicsScale = glm::max(glm::abs(worldTransform.Scale), glm::vec3(0.001f));
				const glm::mat4 transform = GetPhysicsColliderBodyTransform(worldTransform)
					* glm::scale(glm::mat4(1.0f), physicsScale);

				FrameRenderPacket::ColliderDebugItem item;
				item.Mesh = staticMesh;
				item.Source = meshSource;
				item.Transform = transform;
				item.SimpleCollider = false;
				packet.ColliderDebug.push_back(std::move(item));
			}
		}
	}

	void Scene::OnRenderEditor(Ref<SceneRenderer> renderer, const EditorCamera& camera, const std::function<bool(Entity)>& isSelected)
	{
		Render3D(camera, renderer, isSelected);
	}

	void Scene::OnRenderSimulation(Ref<SceneRenderer> renderer, const EditorCamera& camera, const std::function<bool(Entity)>& isSelected)
	{
		Render3D(camera, renderer, isSelected);
	}

	void Scene::OnRenderRuntime(Ref<SceneRenderer> renderer)
	{
		Render3DRuntime(renderer);
	}

	std::unordered_set<AssetHandle> Scene::GetAssetList()
	{
		std::unordered_set<AssetHandle> assets;
		auto addIfValid = [&assets](AssetHandle handle)
		{
			if (handle && !AssetManager::GetMemoryAsset(handle))
				assets.insert(handle);
		};

		auto addMaterialDependencies = [&addIfValid](AssetHandle materialHandle)
		{
			addIfValid(materialHandle);

			if (!materialHandle || AssetManager::GetAssetType(materialHandle) != AssetType::Material)
				return;

			Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
			if (!materialAsset)
				return;

			addIfValid(materialAsset->GetAlbedoMapHandle());
			addIfValid(materialAsset->GetNormalMapHandle());
			addIfValid(materialAsset->GetMetalnessMapHandle());
			addIfValid(materialAsset->GetRoughnessMapHandle());
		};

		auto addMaterialTableDependencies = [&addMaterialDependencies](const Ref<MaterialTable>& materialTable)
		{
			if (!materialTable)
				return;

			for (const auto& [index, material] : materialTable->GetMaterials())
			{
				(void)index;
				addMaterialDependencies(material);
			}
		};

		auto addMeshSourceDependencies = [&addIfValid, &addMaterialDependencies](AssetHandle meshSourceHandle)
		{
			addIfValid(meshSourceHandle);

			if (!meshSourceHandle || AssetManager::GetAssetType(meshSourceHandle) != AssetType::MeshSource)
				return;

			Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(meshSourceHandle);
			if (!meshSource)
				return;

			for (AssetHandle material : meshSource->GetMaterials())
				addMaterialDependencies(material);
		};

		auto addMeshDependencies = [&addIfValid, &addMaterialTableDependencies, &addMeshSourceDependencies](AssetHandle meshHandle)
		{
			addIfValid(meshHandle);

			if (!meshHandle)
				return;

			const AssetType assetType = AssetManager::GetAssetType(meshHandle);
			if (assetType == AssetType::MeshSource)
			{
				addMeshSourceDependencies(meshHandle);
			}
			else if (assetType == AssetType::Mesh)
			{
				Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshHandle);
				if (!mesh)
					return;

				addMeshSourceDependencies(mesh->GetMeshSource());
				addMaterialTableDependencies(mesh->GetMaterials());
			}
			else if (assetType == AssetType::StaticMesh)
			{
				Ref<StaticMesh> staticMesh = AssetManager::GetAsset<StaticMesh>(meshHandle);
				if (!staticMesh)
					return;

				addMeshSourceDependencies(staticMesh->GetMeshSource());
				addMaterialTableDependencies(staticMesh->GetMaterials());
			}
		};

		auto prefabView = m_Registry.view<PrefabComponent>();
		for (auto entity : prefabView)
			addIfValid(prefabView.get<PrefabComponent>(entity).PrefabID);

		auto spriteView = m_Registry.view<SpriteRendererComponent>();
		for (auto entity : spriteView)
			addIfValid(spriteView.get<SpriteRendererComponent>(entity).Texture);

		auto textView = m_Registry.view<TextComponent>();
		for (auto entity : textView)
			addIfValid(textView.get<TextComponent>(entity).FontHandle);

		auto meshView = m_Registry.view<MeshComponent>();
		for (auto entity : meshView)
			addMeshDependencies(meshView.get<MeshComponent>(entity).Mesh);

		auto submeshView = m_Registry.view<SubmeshComponent>();
		for (auto entity : submeshView)
		{
			const auto& submesh = submeshView.get<SubmeshComponent>(entity);
			addMeshDependencies(submesh.Mesh);
			addMaterialTableDependencies(submesh.MaterialTable);
		}

		auto staticMeshView = m_Registry.view<StaticMeshComponent>();
		for (auto entity : staticMeshView)
		{
			const auto& staticMesh = staticMeshView.get<StaticMeshComponent>(entity);
			addMeshDependencies(staticMesh.StaticMesh);
			addMaterialTableDependencies(staticMesh.MaterialTable);
		}

		auto meshColliderView = m_Registry.view<MeshColliderComponent>();
		for (auto entity : meshColliderView)
			addMeshDependencies(meshColliderView.get<MeshColliderComponent>(entity).ColliderAsset);

		auto skyLightView = m_Registry.view<SkyLightComponent>();
		for (auto entity : skyLightView)
			addIfValid(skyLightView.get<SkyLightComponent>(entity).SceneEnvironment);

		return assets;
	}

	// ============================================================================

	template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component)
	{
		static_assert(sizeof(T) == 0);
	}

	template<>
	void Scene::OnComponentAdded<IDComponent>(Entity entity, IDComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<FolderComponent>(Entity entity, FolderComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<RelationshipComponent>(Entity entity, RelationshipComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
	{
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
			component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}

	template<>
	void Scene::OnComponentAdded<ScriptComponent>(Entity entity, ScriptComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<SpriteRendererComponent>(Entity entity, SpriteRendererComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<CircleRendererComponent>(Entity entity, CircleRendererComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<RigidBody2DComponent>(Entity entity, RigidBody2DComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<BoxCollider2DComponent>(Entity entity, BoxCollider2DComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<CircleCollider2DComponent>(Entity entity, CircleCollider2DComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<RigidBodyComponent>(Entity entity, RigidBodyComponent& component)
	{
		(void)component;
		if (m_PhysicsScene)
			m_PhysicsScene->CreateBody(entity);
	}

	template<>
	void Scene::OnComponentAdded<CharacterControllerComponent>(Entity entity, CharacterControllerComponent& component)
	{
		(void)component;
		if (m_PhysicsScene)
			m_PhysicsScene->CreateCharacterController(entity);
	}

	template<>
	void Scene::OnComponentAdded<CompoundColliderComponent>(Entity entity, CompoundColliderComponent& component)
	{
		(void)entity;
		(void)component;
	}

	template<>
	void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, BoxColliderComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, SphereColliderComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<CapsuleColliderComponent>(Entity entity, CapsuleColliderComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<MeshColliderComponent>(Entity entity, MeshColliderComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<TextComponent>(Entity entity, TextComponent& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<AudioData>(Entity entity, AudioData& component)
	{

	}

	template<>
	void Scene::OnComponentAdded<AudioSourceComponent>(Entity entity, AudioSourceComponent& component)
	{
		(void)component;
		ReleaseRuntimeAudio(entity);
	}

	template<>
	void Scene::OnComponentAdded<AudioListenerComponent>(Entity entity, AudioListenerComponent& component)
	{
		if (component.Listener)
			component.Listener->SetConfig(component.Config);
	}

	// 3D Component specializations

	template<>
	void Scene::OnComponentAdded<MeshComponent>(Entity entity, MeshComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<MeshTagComponent>(Entity entity, MeshTagComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<PrefabComponent>(Entity entity, PrefabComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<StaticMeshComponent>(Entity entity, StaticMeshComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<SubmeshComponent>(Entity entity, SubmeshComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<DirectionalLightComponent>(Entity entity, DirectionalLightComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<PointLightComponent>(Entity entity, PointLightComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<SpotLightComponent>(Entity entity, SpotLightComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<SkyLightComponent>(Entity entity, SkyLightComponent& component)
	{
	}

}
