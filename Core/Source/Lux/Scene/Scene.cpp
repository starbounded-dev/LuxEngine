#include "lpch.h"

#include "Lux/Scene/Scene.h"

#include "Lux/Asset/AssetManager.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioSource.h"
#include "Lux/Audio/AudioListener.h"

#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scene/ScriptableEntity.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Physics/ContactListener2D.h"
#include "Lux/Project/Project.h"

#include <glm/glm.hpp>

#include <thread>

// Box2D
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_circle_shape.h"

namespace Lux {

	static ContactListener2D s_Box2DContactListener;

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
	}

	Scene::Scene()
	{
		m_Renderer2D = Ref<Renderer2D>::Create();
	}

	Scene::~Scene()
	{
		ReleaseAllRuntimeAudio();
		m_EntityMap.clear();
		delete m_PhysicsWorld;
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
			const auto& name = srcSceneRegistry.get<TagComponent>(e).Tag;
			Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);
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

		ContactListener2D::m_IsPlaying = true;

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
		/*
		// Scripting
		{
			ScriptEngine::OnRuntimeStart(this);
			// Instantiate all scripts entities

			auto view = m_Registry.view<ScriptComponent>();
			for (auto e : view)
			{
				Entity entity = { e, this };
				ScriptEngine::OnCreateEntity(entity);
			}
		}*/
	}

	void Scene::OnRuntimeStop()
	{
		m_IsRunning = false;

		ContactListener2D::m_IsPlaying = false;

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

		//ScriptEngine::OnRuntimeStop();
	}

	void Scene::OnSimulationStart()
	{
		OnPhysics2DStart();
	}

	void Scene::OnSimulationStop()
	{
		OnPhysics2DStop();
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			// Update scripts
			{
				auto view = m_Registry.view<ScriptComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					ScriptEngine::OnUpdateEntity(entity, ts);
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

			// Physics
			{
				const int32_t velocityIterations = 6;
				const int32_t positionIterations = 2;
				m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

				// Retrieve transform from Box2D
				auto view = m_Registry.view<RigidBody2DComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					auto& transform = entity.GetComponent<TransformComponent>();
					auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

					b2Body* body = (b2Body*)rb2d.RuntimeBody;

					const auto& position = body->GetPosition();
					transform.Translation.x = position.x;
					transform.Translation.y = position.y;
					glm::vec3 rotation = transform.GetRotationEuler();
					rotation.z = body->GetAngle();
					transform.SetRotationEuler(rotation);
				}
			}

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
			// Physics
			{
				const int32_t velocityIterations = 6;
				const int32_t positionIterations = 2;
				m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

				// Retrieve transform from Box2D
				auto view = m_Registry.view<RigidBody2DComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					auto& transform = entity.GetComponent<TransformComponent>();
					auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

					b2Body* body = (b2Body*)rb2d.RuntimeBody;
					const auto& position = body->GetPosition();
					transform.Translation.x = position.x;
					transform.Translation.y = position.y;
					glm::vec3 rotation = transform.GetRotationEuler();
					rotation.z = body->GetAngle();
					transform.SetRotationEuler(rotation);
				}
			}
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

	Entity Scene::DuplicateEntity(Entity entity)
	{
		using DuplicateComponents =
			ComponentGroup<TransformComponent, SpriteRendererComponent, CircleRendererComponent, CameraComponent, ScriptComponent,
			NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent, TextComponent,
			MeshComponent, MeshTagComponent, PrefabComponent, StaticMeshComponent, SubmeshComponent,
			DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent>;

		if (!entity)
			return {};

		std::function<Entity(Entity, Entity)> duplicateHierarchy;
		duplicateHierarchy = [&](Entity source, Entity parent) -> Entity
		{
			Entity destination = CreateEntity(source.GetName());
			CopyComponentIfExists(DuplicateComponents{}, destination, source);

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
			NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent, TextComponent,
			MeshComponent, MeshTagComponent, StaticMeshComponent, SubmeshComponent,
			DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent>;

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
		// Guard against double-call leak
		if (m_PhysicsWorld)
		{
			delete m_PhysicsWorld;
			m_PhysicsWorld = nullptr;
		}

		m_PhysicsWorld = new b2World({ 0.0f, -9.8f });

		auto view = m_Registry.view<RigidBody2DComponent>();
		for (auto e : view)
		{
			Entity entity = { e, this };
			auto& transform = entity.GetComponent<TransformComponent>();
			auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

			b2BodyDef bodyDef;
			bodyDef.type = Utils::RigidBody2DTypeToBox2DBody(rb2d.BodyType);
			bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
			bodyDef.angle = transform.GetRotationEuler().z;
			bodyDef.linearDamping = rb2d.LinearDrag;
			bodyDef.angularDamping = rb2d.AngularDrag;
			bodyDef.gravityScale = rb2d.GravityScale;
			bodyDef.bullet = rb2d.IsBullet;

			b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
			body->SetFixedRotation(rb2d.FixedRotation);
			rb2d.RuntimeBody = body;

			if (entity.HasComponent<BoxCollider2DComponent>())
			{
				auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

				b2PolygonShape boxShape;
				boxShape.SetAsBox(bc2d.Size.x * transform.Scale.x, bc2d.Size.y * transform.Scale.y, b2Vec2(bc2d.Offset.x, bc2d.Offset.y), 0.0f);

				b2FixtureDef fixtureDef;
				fixtureDef.shape = &boxShape;
				fixtureDef.density = bc2d.Density;
				fixtureDef.friction = bc2d.Friction;
				fixtureDef.restitution = bc2d.Restitution;
				fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
				body->CreateFixture(&fixtureDef);
			}

			if (entity.HasComponent<CircleCollider2DComponent>())
			{
				auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();

				b2CircleShape circleShape;
				circleShape.m_p.Set(cc2d.Offset.x, cc2d.Offset.y);
				circleShape.m_radius = transform.Scale.x * cc2d.Radius;

				b2FixtureDef fixtureDef;
				fixtureDef.shape = &circleShape;
				fixtureDef.density = cc2d.Density;
				fixtureDef.friction = cc2d.Friction;
				fixtureDef.restitution = cc2d.Restitution;
				fixtureDef.restitutionThreshold = cc2d.RestitutionThreshold;
				body->CreateFixture(&fixtureDef);
			}
		}
	}

	void Scene::OnPhysics2DStop()
	{
		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
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
				lightEnv.DirectionalLights[dirLightIndex].Radiance = dirLight.Radiance;
				lightEnv.DirectionalLights[dirLightIndex].Intensity = dirLight.Intensity;
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
				pl.Radiance = pointLight.Radiance;
				pl.Intensity = pointLight.Intensity;
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
				sl.Radiance = spotLight.Radiance;
				sl.Intensity = spotLight.Intensity;
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

	void Scene::SubmitStaticMeshes(Ref<SceneRenderer> renderer,
		const std::function<bool(Entity)>& isSelected) const
	{
		struct StaticMeshSubmission
		{
			Ref<StaticMesh> StaticMeshAsset;
			Ref<MeshSource> MeshSourceAsset;
			Ref<MaterialTable> Materials;
			TransformComponent LocalTransform;
			glm::mat4 WorldTransform{ 1.0f };
			bool ComputeLocalTransform = false;
			bool Selected = false;
		};

		std::vector<StaticMeshSubmission> submissions;
		auto view = m_Registry.view<const TransformComponent, const StaticMeshComponent>();
		for (auto e : view)
		{
			Entity entity = { e, const_cast<Scene*>(this) };
			const auto& meshComp = view.get<const StaticMeshComponent>(e);

			if (!meshComp.Visible)
				continue;

			if (!meshComp.StaticMesh)
				continue;

			Ref<StaticMesh> staticMesh = StaticMesh::GetOrCreateRuntime(meshComp.StaticMesh);
			if (!staticMesh)
				continue;

			AssetHandle meshSourceHandle = staticMesh->GetMeshSource();
			Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(meshSourceHandle);
			if (!meshSource)
				continue;

			Ref<MaterialTable> materialTable = meshComp.MaterialTable && !meshComp.MaterialTable->GetMaterials().empty()
				? meshComp.MaterialTable
				: staticMesh->GetMaterials();

			StaticMeshSubmission& submission = submissions.emplace_back();
			submission.StaticMeshAsset = staticMesh;
			submission.MeshSourceAsset = meshSource;
			submission.Materials = materialTable;
			submission.Selected = isSelected ? isSelected(entity) : false;

			const bool hasParent = entity.HasComponent<RelationshipComponent>()
				&& entity.GetComponent<RelationshipComponent>().ParentHandle != 0;
			if (hasParent)
				submission.WorldTransform = GetWorldSpaceTransformMatrix(entity);
			else
			{
				submission.LocalTransform = view.get<const TransformComponent>(e);
				submission.ComputeLocalTransform = true;
			}
		}

		auto computeRange = [&](size_t begin, size_t end)
			{
				for (size_t index = begin; index < end; index++)
				{
					StaticMeshSubmission& submission = submissions[index];
					if (submission.ComputeLocalTransform)
						submission.WorldTransform = submission.LocalTransform.GetTransform();
				}
			};

		constexpr size_t parallelTransformThreshold = 512;
		const uint32_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
		if (submissions.size() >= parallelTransformThreshold && hardwareThreads > 1)
		{
			const uint32_t workerCount = std::min<uint32_t>(hardwareThreads, static_cast<uint32_t>((submissions.size() + parallelTransformThreshold - 1) / parallelTransformThreshold));
			const size_t chunkSize = (submissions.size() + workerCount - 1) / workerCount;
			std::vector<std::thread> workers;
			workers.reserve(workerCount);

			for (uint32_t worker = 0; worker < workerCount; worker++)
			{
				const size_t begin = worker * chunkSize;
				const size_t end = std::min(submissions.size(), begin + chunkSize);
				if (begin >= end)
					continue;

				workers.emplace_back(computeRange, begin, end);
			}

			for (std::thread& worker : workers)
				worker.join();
		}
		else
		{
			computeRange(0, submissions.size());
		}

		for (const StaticMeshSubmission& submission : submissions)
		{
			if (submission.Selected)
				renderer->SubmitSelectedStaticMesh(submission.StaticMeshAsset, submission.MeshSourceAsset, submission.Materials, submission.WorldTransform);
			else
				renderer->SubmitStaticMesh(submission.StaticMeshAsset, submission.MeshSourceAsset, submission.Materials, submission.WorldTransform);
		}
	}

	void Scene::Render3D(const EditorCamera& camera, Ref<SceneRenderer> renderer,
		const std::function<bool(Entity)>& isSelected)
	{
		if (!renderer || !renderer->IsReady())
			return;

		// Set up the SceneRendererCamera from the EditorCamera
		SceneRendererCamera sceneCamera;
		sceneCamera.Camera.SetProjectionMatrix(camera.GetProjectionMatrix(), camera.GetUnReversedProjectionMatrix());
		sceneCamera.ViewMatrix = camera.GetViewMatrix();
		sceneCamera.Near = camera.GetNearClip();
		sceneCamera.Far = camera.GetFarClip();
		sceneCamera.FOV = camera.GetVerticalFOV();

		// Collect and set light environment from DirectionalLight and PointLight components
		LightEnvironment lightEnv = CollectLightEnvironment();
		renderer->SetLightEnvironment(lightEnv);

		// Collect and set skybox/IBL environment from SkyLightComponent
		float envIntensity = 1.0f;
		float envLod = 0.0f;
		Ref<Environment> environment = CollectEnvironment(envIntensity, envLod);
		renderer->SetEnvironment(environment, envIntensity, envLod);

		// Begin the 3D rendering frame after scene lighting/environment state is prepared
		renderer->BeginScene(sceneCamera);

		// Submit all static meshes for rendering
		SubmitStaticMeshes(renderer, isSelected);

		// End the frame and execute the render passes
		renderer->EndScene();

		// Composite 2D content onto the SceneRenderer output so the main viewport
		// can display a single image containing both 3D and 2D, like Hazel.
		if (renderer->GetFinalPassImage())
		{
			Ref<Renderer2D> renderer2D = renderer->GetRenderer2D();
			if (renderer2D)
			{
				renderer2D->SetTargetFramebuffer(renderer->GetExternalCompositeFramebuffer());
				renderer2D->ResetStats();
				renderer2D->BeginScene(camera.GetViewProjection(), camera.GetViewMatrix());

				// Draw sprites
				{
					auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
					for (auto entity : group)
					{
						auto& sprite = group.get<SpriteRendererComponent>(entity);

						Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(sprite.Texture);
						const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(Entity{ entity, this });
						if (texture)
							renderer2D->DrawQuad(worldTransform, texture, sprite.TilingFactor, sprite.Color);
						else
							renderer2D->DrawQuad(worldTransform, sprite.Color);
					}
				}

				// Draw circles
				{
					auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
					for (auto entity : view)
					{
						auto& circle = view.get<CircleRendererComponent>(entity);
						renderer2D->DrawCircle(GetWorldSpaceTransformMatrix(Entity{ entity, this }), circle.Color);
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
							renderer2D->DrawString(text.TextString, font, GetWorldSpaceTransformMatrix(Entity{ entity, this }),
								text.MaxWidth, text.Color, text.LineSpacing, text.Kerning);
						}
					}
				}

				renderer2D->EndScene();
			}
		}
	}

	void Scene::Render3DRuntime(Ref<SceneRenderer> renderer)
	{
		if (!renderer || !renderer->IsReady())
			return;

		// Find the primary camera entity
		Entity cameraEntity = GetPrimaryCameraEntity();
		if (!cameraEntity)
			return;

		const auto& cameraComp = cameraEntity.GetComponent<CameraComponent>();

		// Set up the SceneRendererCamera from the runtime camera
		SceneRendererCamera sceneCamera;
		sceneCamera.Camera.SetProjectionMatrix(cameraComp.Camera.GetProjectionMatrix(), cameraComp.Camera.GetUnReversedProjectionMatrix());
		sceneCamera.ViewMatrix = glm::inverse(GetWorldSpaceTransformMatrix(cameraEntity));

		// Collect and set light environment
		LightEnvironment lightEnv = CollectLightEnvironment();
		renderer->SetLightEnvironment(lightEnv);

		// Collect and set skybox/IBL environment
		float envIntensity = 1.0f;
		float envLod = 0.0f;
		Ref<Environment> environment = CollectEnvironment(envIntensity, envLod);
		renderer->SetEnvironment(environment, envIntensity, envLod);

		// Begin the 3D rendering frame after scene lighting/environment state is prepared
		renderer->BeginScene(sceneCamera);

		// Submit all static meshes (no selection highlight in runtime)
		SubmitStaticMeshes(renderer, nullptr);

		// End the frame
		renderer->EndScene();

		// Composite 2D content onto the SceneRenderer output.
		if (renderer->GetFinalPassImage())
		{
			Ref<Renderer2D> renderer2D = renderer->GetRenderer2D();
			if (renderer2D)
			{
				const glm::mat4 view = sceneCamera.ViewMatrix;
				const glm::mat4 viewProjection = cameraComp.Camera.GetProjectionMatrix() * view;

				renderer2D->SetTargetFramebuffer(renderer->GetExternalCompositeFramebuffer());
				renderer2D->ResetStats();
				renderer2D->BeginScene(viewProjection, view);

				// Draw sprites
				{
					auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
					for (auto entity : group)
					{
						auto& sprite = group.get<SpriteRendererComponent>(entity);

						Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(sprite.Texture);
						const glm::mat4 worldTransform = GetWorldSpaceTransformMatrix(Entity{ entity, this });
						if (texture)
							renderer2D->DrawQuad(worldTransform, texture, sprite.TilingFactor, sprite.Color);
						else
							renderer2D->DrawQuad(worldTransform, sprite.Color);
					}
				}

				// Draw circles
				{
					auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
					for (auto entity : view)
					{
						auto& circle = view.get<CircleRendererComponent>(entity);
						renderer2D->DrawCircle(GetWorldSpaceTransformMatrix(Entity{ entity, this }), circle.Color);
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
							renderer2D->DrawString(text.TextString, font, GetWorldSpaceTransformMatrix(Entity{ entity, this }),
								text.MaxWidth, text.Color, text.LineSpacing, text.Kerning);
						}
					}
				}

				renderer2D->EndScene();
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
