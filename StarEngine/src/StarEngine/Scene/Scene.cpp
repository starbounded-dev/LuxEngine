#include "sepch.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "StarEngine/Scene/Scene.h"

#include "StarEngine/Asset/AssetManager.h"

#include "StarEngine/Audio/AudioEngine.h"
#include "StarEngine/Audio/AudioSource.h"
#include "StarEngine/Audio/AudioListener.h"

#include "StarEngine/Core/Application.h"

#include "StarEngine/Scene/Components.h"
#include "StarEngine/Scene/Entity.h"
#include "StarEngine/Scripting/ScriptEngine.h"
#include "StarEngine/Renderer/Renderer2D.h"
#include "StarEngine/Physics/ContactListener2D.h"

#include <glm/glm.hpp>

// Box2D
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_circle_shape.h"
#include "StarEngine/Core/Timer.h"
#include "StarEngine/Core/Events/SceneEvents.h"

namespace StarEngine {

	bool Scene::s_SetPaused = false;
	glm::vec2 Scene::s_Gravity = { 0.0f, -9.81f };

	static ContactListener2D s_Box2DContactListener;

	Scene::Scene()
	{
	}

	Scene::~Scene()
	{
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
	static void CopyComponentIfExists(ComponentGroup<Component...>, Entity dst, Entity src)
	{
		CopyComponentIfExists<Component...>(dst, src);
	}

	Ref<Scene> Scene::Copy(Ref<Scene> other)
	{
		Ref<Scene> newScene = Ref<Scene>::Create();

		newScene->m_ViewportLeft = other->m_ViewportLeft;
		newScene->m_ViewportTop = other->m_ViewportTop;
		newScene->m_ViewportRight = other->m_ViewportRight;
		newScene->m_ViewportBottom = other->m_ViewportBottom;

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
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TransformComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		m_EntityIDMap[uuid] = entity;
		return entity;
	}

	Entity Scene::CreateEntityWithID(UUID uuid, const std::string& name, bool shouldSort)
	{
		SE_PROFILE_FUNCTION("Scene::CreateEntityWithID");

		auto entity = Entity{ m_Registry.create(), this };
		auto& idComponent = entity.AddComponent<IDComponent>();
		idComponent.ID = uuid;

		entity.AddComponent<TransformComponent>();
		if (!name.empty())
			entity.AddComponent<TagComponent>(name);

		entity.AddComponent<RelationshipComponent>();
 
		SE_CORE_ASSERT(m_EntityIDMap.find(uuid) == m_EntityIDMap.end());
		m_EntityIDMap[uuid] = entity;

		if (shouldSort)
			SortEntities();

		return entity;
	}

	void Scene::SortEntities()
	{
		m_Registry.sort<IDComponent>([&](const auto lhs, const auto rhs)
			{
				auto lhsEntity = m_EntityIDMap.find(lhs.ID);
				auto rhsEntity = m_EntityIDMap.find(rhs.ID);
				return static_cast<uint32_t>(lhsEntity->second) < static_cast<uint32_t>(rhsEntity->second);
			});
	}

	void Scene::DestroyEntity(Entity entity)
	{
		m_EntityIDMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
	}

	void Scene::OnRuntimeStart()
	{
		SE_PROFILE_FUNCTION("Scene::OnRuntimeStart");
		SE_CORE_INFO_TAG("Scene", "Starting scene {}", m_Name);

		Timestep ts;

		m_IsRunning = true;

		Ref<Scene> _this = this;

		OnPhysics2DStart();

		ContactListener2D::m_IsPlaying = true;

		{
			auto filter = m_Registry.view<TransformComponent, AudioListenerComponent>();
			filter.each([&](TransformComponent& transform, AudioListenerComponent& ac)
				{
					ac.Listener = Ref<AudioListener>::Create();
					if (ac.Active)
					{
						const glm::mat4 inverted = glm::inverse(transform.GetTransform());
						const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));
						ac.Listener->SetConfig(ac.Config);
						ac.Listener->SetPosition(glm::vec4(transform.Translation, 1.0f));
						ac.Listener->SetDirection(glm::vec3{ -forward.x, -forward.y, -forward.z });
					}
				});
		}

		{
			auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
			view.each([&](TransformComponent& transform, AudioSourceComponent& ac)
				{
					if (AssetManager::IsAssetHandleValid(ac.Audio))
					{
						if (ac.Audio && !ac.AudioSourceData.UsePlaylist)
						{
							Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(ac.Audio);
							const glm::mat4 inverted = glm::inverse(transform.GetTransform());
							const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));

							if (audioSource != nullptr)
							{
								audioSource->SetConfig(ac.Config);
								audioSource->SetPosition(glm::vec4(transform.Translation, 1.0f));
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
								Ref<AudioSource> playingSourceIndex = AssetManager::GetAsset<AudioSource>(ac.AudioSourceData.Playlist[ac.AudioSourceData.CurrentIndex]);
								const glm::mat4 inverted = glm ::inverse(transform.GetTransform());
								const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));

								if (playingSourceIndex != nullptr)
								{
									playingSourceIndex->SetConfig(ac.Config);
									playingSourceIndex->SetPosition(glm::vec4(transform.Translation, 1.0f));
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

		{
			auto view = m_Registry.view<ScriptComponent>();
			const auto& scriptEngine = ScriptEngine::GetInstance();
			{
				SE_PROFILE_SCOPE("Scene::OnUpdate - C# OnUpdate");
				for (auto scriptEntityID : view)
				{
					auto& scriptComponent = view.get<ScriptComponent>(scriptEntityID);

					if (!scriptEngine.IsValidScript(scriptComponent.ScriptID) || !scriptComponent.Instance.IsValid())
					{
						SE_CORE_ERROR("Entity {} has invalid script!", Entity(scriptEntityID, this).GetComponent<TagComponent>().Tag);
						continue;
					}

					scriptComponent.Instance.Invoke<float>("OnUpdate", ts);
				}
			}
			{
				SE_PROFILE_SCOPE("Scene::OnUpdate - C# OnLateUpdate");
				Timer timer;

				for (auto scriptEntityID : view)
				{
					auto& scriptComponent = view.get<ScriptComponent>(scriptEntityID);

					if (!scriptEngine.IsValidScript(scriptComponent.ScriptID))
					{
						continue;
					}

					scriptComponent.Instance.Invoke<float>("OnLateUpdate", ts);
				}
				m_PerformanceTimers.ScriptLateUpdate = timer.ElapsedMillis();
			}

		}
	}

	void Scene::OnRuntimeStop()
	{
		m_IsRunning = false;

		Ref<Scene> _this = this;

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
							Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(ac.Audio);

							if (audioSource != nullptr && audioSource->IsPlaying())
								audioSource->Stop();
						}
						else if (ac.Audio && ac.AudioSourceData.UsePlaylist)
						{
							ac.AudioSourceData.CurrentIndex = ac.AudioSourceData.StartIndex;
							ac.AudioSourceData.PlayingCurrentIndex = false;

							for (auto audio : ac.AudioSourceData.Playlist)
							{
								Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(audio);

								if (audioSource != nullptr && audioSource->IsPlaying())
									audioSource->Stop();
							}
						}
					}
				});
		}

		auto& scriptEngine = ScriptEngine::GetMutable();

		auto view = m_Registry.view<IDComponent, ScriptComponent>();
		for (auto scriptEntityID : view)
		{
			const auto& idComponent = view.get<IDComponent>(scriptEntityID);
			auto& scriptComponent = view.get<ScriptComponent>(scriptEntityID);

			if (!scriptEngine.IsValidScript(scriptComponent.ScriptID))
			{
				continue;
			}

			if (!m_ScriptStorage.EntityStorage.contains(idComponent.ID))
			{
				// Shouldn't happen
				SE_CORE_VERIFY(false);
			}

			scriptComponent.Instance.Invoke("OnDestroy");

			scriptEngine.DestroyInstance(idComponent.ID, m_ScriptStorage);
		}
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
		if ((!m_IsPaused && !s_SetPaused) || m_StepFrames-- > 0)
		{
			// Physics
			{
				const int32_t velocityIterations = 6;
				const int32_t positionIterations = 2;
				m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

				// Retrieve transform from Box2D
				auto view = m_Registry.view<RigidBody2DComponent>();
				for (auto entity : view)
				{
					Entity e = { entity, this };
					auto& rb2d = e.GetComponent<RigidBody2DComponent>();

					if (rb2d.RuntimeBody == nullptr)
						continue;

					b2Body* body = static_cast<b2Body*>(rb2d.RuntimeBody);

					auto& position = body->GetPosition();
					auto& transform = e.GetComponent<TransformComponent>();
					transform.Translation.x = position.x;
					transform.Translation.y = position.y;
					glm::vec3 rotation = transform.GetRotationEuler();
					rotation.z = body->GetAngle();
					transform.SetRotationEuler(rotation);
				}
			}

			{
				SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioListenerComponent Scope", 0xFF7200);

				auto view = m_Registry.view<AudioListenerComponent>();
				view.each([&](entt::entity entity, AudioListenerComponent& alc)
					{
						Entity e = { entity, this };
						auto& ac = e.GetComponent<AudioListenerComponent>();
						auto& transform = e.GetComponent<TransformComponent>();

						if (ac.Active)
						{
							const glm::mat4 inverted = glm::inverse(transform.GetTransform());
							const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));
							ac.Listener->SetPosition(glm::vec4(transform.Translation, 1.0f));
							ac.Listener->SetDirection(glm::vec3{ -forward.x, -forward.y, -forward.z });
							//break;
						}
					});
			}

			{
				SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent Scope", 0xFF7200);

				auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
				view.each([&](entt::entity entity, TransformComponent& transform, AudioSourceComponent& asc)
					{
						//Entity e = { entity, this };
						//auto& transform = e.GetComponent<TransformComponent>();

						//const glm::mat4 inverted = glm::inverse(transform.GetTransform());
						//const glm::vec3 forward = glm::vector_normalize3(inverted.Value.z_axis);

						if (asc.Audio && !asc.AudioSourceData.UsePlaylist)
						{
							Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(asc.Audio);
							if (!audioSource->IsPlaying() && asc.Paused)
							{
								audioSource->SetConfig(asc.Config);
								audioSource->Play();
								asc.Paused = false;
							}

							if (audioSource != nullptr)
							{
								audioSource->SetConfig(asc.Config);
								audioSource->SetPosition(glm::vec4(transform.Translation, 1.0f));
								//audioSource->SetDirection(forward);
							}
						}
						else if (asc.Audio && asc.AudioSourceData.UsePlaylist)
						{
							SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent 2 Scope", 0xEE3AFF);

							Ref<AudioSource> audioSourceIndex = AssetManager::GetAsset<AudioSource>(asc.AudioSourceData.Playlist[asc.AudioSourceData.OldIndex]);

							//if (ac.AudioSourceData.OldIndex <= ac.AudioSourceData.Playlist.size() - 1)
							if (asc.AudioSourceData.CurrentIndex < asc.AudioSourceData.Playlist.size() && audioSourceIndex != nullptr && asc.Config.PlayOnAwake && !audioSourceIndex->IsPlaying() && !asc.Paused)
							{
								SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent 3 Scope", 0xFF8E68);

								audioSourceIndex = AssetManager::GetAsset<AudioSource>(asc.AudioSourceData.Playlist[asc.AudioSourceData.CurrentIndex]);

								if (!audioSourceIndex->IsLooping())
								{
									SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent 4 Scope", 0xFF2F68);

									audioSourceIndex->SetConfig(asc.Config);
									audioSourceIndex->Play();
									asc.AudioSourceData.PlayingCurrentIndex = true;
									asc.Paused = false;

									//const rtmcpp::Mat4 inverted = rtmcpp::Inverse(transform.GetTransform());
									//const rtmcpp::Vec3 forward = rtm::vector_normalize3(inverted.Value.z_axis);

									audioSourceIndex->SetConfig(asc.Config);
									audioSourceIndex->SetPosition(glm::vec4(transform.Translation, 1.0f));
									//audioSourceIndex->SetDirection(forward);

									if (asc.AudioSourceData.RepeatAfterSpecificTrackPlays && asc.AudioSourceData.CurrentIndex == asc.AudioSourceData.StartIndex)
									{
										SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent 5 Scope", 0xA191FF);

										audioSourceIndex->SetLooping(true);
									}

									if (asc.AudioSourceData.OldIndex != asc.AudioSourceData.CurrentIndex)
									{
										SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent 6 Scope", 0x8CCBFF);

										asc.AudioSourceData.OldIndex = asc.AudioSourceData.CurrentIndex;
									}

									asc.AudioSourceData.CurrentIndex++;
								}
							}
							else if (asc.AudioSourceData.CurrentIndex < asc.AudioSourceData.Playlist.size() && audioSourceIndex != nullptr && asc.Config.PlayOnAwake && asc.Paused)
							{
								audioSourceIndex->SetConfig(asc.Config);
								audioSourceIndex->Play();
								asc.AudioSourceData.PlayingCurrentIndex = true;
								asc.Paused = false;
							}

							if (asc.AudioSourceData.RepeatPlaylist && !asc.AudioSourceData.RepeatAfterSpecificTrackPlays && asc.AudioSourceData.CurrentIndex >= asc.AudioSourceData.Playlist.size())
							{
								if (audioSourceIndex != nullptr && !audioSourceIndex->IsPlaying())
									asc.AudioSourceData.CurrentIndex = 0;
							}

							if (asc.AudioSourceData.RepeatAfterSpecificTrackPlays && !asc.AudioSourceData.RepeatPlaylist && asc.AudioSourceData.CurrentIndex > asc.AudioSourceData.StartIndex)
							{
								if (audioSourceIndex != nullptr && !audioSourceIndex->IsPlaying())
									asc.AudioSourceData.CurrentIndex = asc.AudioSourceData.StartIndex;
							}
						}
					});
			}
		}

		else if (m_IsPaused)
		{
			SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioListenerComponent 2 Scope", 0xFF7200);

			auto view = m_Registry.view<AudioListenerComponent>();
			view.each([&](entt::entity acEntity, AudioListenerComponent& alc)
				{
					Entity e = { acEntity, this };
					auto& ac = e.GetComponent<AudioListenerComponent>();
					auto& transform = e.GetComponent<TransformComponent>();

					if (ac.Active)
					{
						const glm::mat4 inverted = glm::inverse(transform.GetTransform());
						const glm::vec3 forward = glm::normalize(glm::vec3(inverted[2].x, inverted[2].y, inverted[2].z));
						ac.Listener->SetPosition(glm::vec4(transform.Translation, 1.0f));
						ac.Listener->SetDirection(glm::vec3{ -forward.x, -forward.y, -forward.z });
					}
				});


			{
				SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::AudioSourceComponent 2 Scope", 0xFF7200);

				auto view = m_Registry.view<AudioSourceComponent>();
				view.each([&](entt::entity entity, AudioSourceComponent& asc)
					{

						Entity e = { entity , this};
						auto& transform = e.GetComponent<TransformComponent>();

						if (asc.Audio)
						{
							if (!asc.AudioSourceData.UsePlaylist)
							{
								Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(asc.Audio);
								if (audioSource->IsPlaying())
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
									Ref<AudioSource> audioSourceIndex = AssetManager::GetAsset<AudioSource>(asc.Audio);

									if (audioSourceIndex->IsPlaying())
									{
										audioSourceIndex->SetConfig(asc.Config);
										audioSourceIndex->Pause();
										//ac.AudioSourceData.PlayingCurrentIndex = false;
										asc.Paused = true;
									}
								}
								else if (asc.AudioSourceData.OldIndex > 0)
								{
									Ref<AudioSource> audioSourceIndex = AssetManager::GetAsset<AudioSource>(asc.AudioSourceData.Playlist[asc.AudioSourceData.OldIndex]);

									if (asc.AudioSourceData.OldIndex < asc.AudioSourceData.Playlist.size())
									{
										if (audioSourceIndex->IsPlaying())
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

		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			SE_PROFILE_SCOPE_COLOR("Scene::OnUpdateRuntime::ScriptComponent Scope", 0xFF7200);

			// Update Scripts
			auto filter = m_Registry.view<IDComponent, ScriptComponent>();
			filter.each([&](IDComponent& id, ScriptComponent& sc)
				{
					sc.Instance.Invoke<float>("OnUpdate", ts);
				});
		}
		/*
		if (m_IsPlaying)
		{
			auto view = m_Registry.view<ScriptComponent>();
			const auto& scriptEngine = ScriptEngine::GetInstance();
			{
				HZ_PROFILE_SCOPE("Scene::OnUpdate - C# OnLateUpdate");
				Timer timer;

				for (auto scriptEntityID : view)
				{
					auto& scriptComponent = view.get<ScriptComponent>(scriptEntityID);

					if (!scriptEngine.IsValidScript(scriptComponent.ScriptID))
					{
						continue;
					}

					scriptComponent.Instance.Invoke<float>("OnLateUpdate", ts);
				}
				m_PerformanceTimers.ScriptLateUpdate = timer.ElapsedMillis();
			}

			for (auto&& fn : m_PostUpdateQueue)
				fn();
			m_PostUpdateQueue.clear();
		}*/

		// Render 2D
		Camera* mainCamera = nullptr;
		glm::mat4 cameraTransform;
		{
			auto view = m_Registry.view<TransformComponent, CameraComponent>();
			for (auto entity : view)
			{
				auto [transform, camera] = view.get<TransformComponent, CameraComponent>(entity);

				if (camera.Primary)
				{
					// camera.Camera is a SceneCamera (value) → take address
					mainCamera = &camera.Camera;
					cameraTransform = transform.GetTransform();
					break;
				}
			}
		}

		if (mainCamera)
		{
			Renderer2D::BeginScene(*mainCamera, cameraTransform);

			// Draw sprites
			{
				auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
				for (auto entity : group)
				{
					auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);

					Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
				}
			}

			// Draw circles
			{
				auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
				for (auto entity : view)
				{
					auto [transform, circle] = view.get<TransformComponent, CircleRendererComponent>(entity);

					Renderer2D::DrawCircle(transform.GetTransform(), circle.Color, circle.Thickness, circle.Fade, (int)entity);
				}
			}

			// Draw text
			{
				auto view = m_Registry.view<TransformComponent, TextComponent>();
				for (auto entity : view)
				{
					auto [transform, text] = view.get<TransformComponent, TextComponent>(entity);

					Renderer2D::DrawString(text.TextString, transform.GetTransform(), text, (int)entity);
				}
			}

			Renderer2D::EndScene();
		}

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
	}

	void Scene::OnUpdateEditor(Timestep ts, EditorCamera& camera)
	{
		// Render 2D
		RenderScene(camera);
	}

	void Scene::SetViewportBounds(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
	{
		m_ViewportLeft = left;
		m_ViewportTop = top;
		m_ViewportRight = right;
		m_ViewportBottom = bottom;
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
		SE_PROFILE_FUNCTION("Scene::DuplicateEntity");

		auto parentNewEntity = [&entity, scene = this](Entity newEntity)
		{
			if (auto parent = entity.GetParent(); parent)
			{
				newEntity.SetParentUUID(parent.GetUUID());
				parent.Children().push_back(newEntity.GetUUID());
			}
		};

		Entity newEntity;
		if (entity.HasComponent<TagComponent>())
			newEntity = CreateEntity(entity.GetComponent<TagComponent>().Tag);
		else
			newEntity = CreateEntity();

		CopyComponentIfExists<TransformComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		// NOTE(Peter): We can't use this method for copying the RelationshipComponent since we
		//				need to duplicate the entire child hierarchy and basically reconstruct the entire RelationshipComponent from the ground up
		//CopyComponentIfExists<RelationshipComponent>(newEntity.m_EntityHandle, entity.m_EntityHandle, m_Registry);
		// TODO: (0x) When copying MeshTag, we should fix up the entity id
		CopyComponentIfExists<ScriptComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		CopyComponentIfExists<CameraComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		CopyComponentIfExists<SpriteRendererComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		CopyComponentIfExists<TextComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		CopyComponentIfExists<BoxCollider2DComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		CopyComponentIfExists<CircleCollider2DComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		CopyComponentIfExists<RigidBody2DComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		CopyComponentIfExists<AudioListenerComponent>(newEntity.m_EntityHandle, m_Registry, entity);
		/*
#if _DEBUG && 0
		// Check that nothing has been forgotten...
		bool foundAll = true;
		m_Registry.visit(entity, [&](entt::id_type type)
		{
			if (type != entt::type_index<RelationshipComponent>().value())
				bool foundOne = false;
			m_Registry.visit(newEntity, [type, &foundOne](entt::id_type newType) {if (newType == type) foundOne = true; });
			foundAll = foundAll && foundOne;
		});
		SE_CORE_ASSERT(foundAll, "At least one component was not duplicated - have you added a new component type and not dealt with it here?");
#endif*/

		auto childIds = entity.Children(); // need to take a copy of children here, because the collection is mutated below
		for (auto childId : childIds)
		{
			Entity childDuplicate = DuplicateEntity(GetEntityWithUUID(childId));

			// At this point childDuplicate is a child of entity, we need to remove it from that entity
			UnparentEntity(childDuplicate, false);

			childDuplicate.SetParentUUID(newEntity.GetUUID());
			newEntity.Children().push_back(childDuplicate.GetUUID());
		}

		parentNewEntity(newEntity);

		if (newEntity.HasComponent<ScriptComponent>())
		{
			const auto& scriptComponent = newEntity.GetComponent<ScriptComponent>();
			m_ScriptStorage.InitializeEntityStorage(scriptComponent.ScriptID, newEntity.GetUUID());
			m_ScriptStorage.CopyEntityStorage(entity.GetUUID(), newEntity.GetUUID(), m_ScriptStorage);
		}

		return newEntity;
	}

	void Scene::ParentEntity(Entity entity, Entity parent)
	{
		SE_PROFILE_FUNCTION("Scene::ParentEntity");

		if (parent.IsDescendantOf(entity))
		{
			UnparentEntity(parent);

			Entity newParent = TryGetEntityWithUUID(entity.GetParentUUID());
			if (newParent)
			{
				UnparentEntity(entity);
				ParentEntity(parent, newParent);
			}
		}
		else
		{
			Entity previousParent = TryGetEntityWithUUID(entity.GetParentUUID());

			if (previousParent)
				UnparentEntity(entity);
		}

		entity.SetParentUUID(parent.GetUUID());
		parent.Children().push_back(entity.GetUUID());

		ConvertToLocalSpace(entity);
	}

	void Scene::UnparentEntity(Entity entity, bool convertToWorldSpace)
	{
		SE_PROFILE_FUNCTION("Scene::UnparentEntity");

		Entity parent = TryGetEntityWithUUID(entity.GetParentUUID());
		if (!parent)
			return;

		auto& parentChildren = parent.Children();
		parentChildren.erase(std::remove(parentChildren.begin(), parentChildren.end(), entity.GetUUID()), parentChildren.end());

		if (convertToWorldSpace)
			ConvertToWorldSpace(entity);

		entity.SetParentUUID(0);
	}

	void Scene::ConvertToLocalSpace(Entity entity)
	{
		SE_PROFILE_FUNCTION("Scene::ConvertToLocalSpace");

		Entity parent = TryGetEntityWithUUID(entity.GetParentUUID());

		if (!parent)
			return;

		auto& transform = entity.Transform();
		glm::mat4 parentTransform = GetWorldSpaceTransformMatrix(parent);
		glm::mat4 localTransform = glm::inverse(parentTransform) * transform.GetTransform();
		transform.SetTransform(localTransform);
	}

	void Scene::ConvertToWorldSpace(Entity entity)
	{
		SE_PROFILE_FUNCTION("Scene::ConvertToWorldSpace");

		Entity parent = TryGetEntityWithUUID(entity.GetParentUUID());

		if (!parent)
			return;

		glm::mat4 transform = GetWorldSpaceTransformMatrix(entity);
		auto& entityTransform = entity.Transform();
		entityTransform.SetTransform(transform);
	}

	glm::mat4 Scene::GetWorldSpaceTransformMatrix(Entity entity)
	{
		SE_PROFILE_FUNCTION("Scene::GetWorldSpaceTransformMatrix");

		glm::mat4 transform(1.0f);

		Entity parent = TryGetEntityWithUUID(entity.GetParentUUID());
		if (parent)
			transform = GetWorldSpaceTransformMatrix(parent);

		return transform * entity.Transform().GetTransform();
	}

	void Scene::SetWorldSpaceTransformMatrix(Entity entity, const glm::mat4& transform)
	{
		SE_PROFILE_FUNCTION("Scene::SetWorldSpaceTransformMatrix");

		Entity parent = TryGetEntityWithUUID(entity.GetParentUUID());
		glm::mat4 parentTransform = parent ? GetWorldSpaceTransformMatrix(parent) : glm::mat4(1.0f);
		glm::mat4 localTransform = glm::inverse(parentTransform) * transform;
		entity.Transform().SetTransform(localTransform);
	}

	// TODO: Definitely cache this at some point
	TransformComponent Scene::GetWorldSpaceTransform(Entity entity)
	{
		SE_PROFILE_FUNCTION("Scene::GetWorldSpaceTransform");

		glm::mat4 transform = GetWorldSpaceTransformMatrix(entity);
		TransformComponent transformComponent;
		transformComponent.SetTransform(transform);
		return transformComponent;
	}

	void Scene::SetWorldSpaceTransform(Entity entity, const TransformComponent& transform)
	{
		SE_PROFILE_FUNCTION("Scene::SetWorldSpaceTransform");

		Entity parent = TryGetEntityWithUUID(entity.GetParentUUID());
		glm::mat4 parentTransform = parent ? GetWorldSpaceTransformMatrix(parent) : glm::mat4(1.0f);
		glm::mat4 localTransform = glm::inverse(parentTransform) * transform.GetTransform();
		entity.Transform().SetTransform(localTransform);
	}

	Entity Scene::FindEntityByTag(const std::string& tag)
	{
		Entity e{};
		auto filter = m_Registry.view<TagComponent>();
		filter.each([&](entt::entity entity, TagComponent& tc)
			{
				const auto& candidate = tc.Tag;

				if (candidate == tag)
					e = Entity{ entity, this };

			});

		return e;
	}

	void Scene::OnSceneTransition(AssetHandle handle)
	{
		if (m_OnSceneTransitionCallback)
			m_OnSceneTransitionCallback(handle);

		// Debug
		if (!m_OnSceneTransitionCallback)
		{
			SE_CORE_WARN("Cannot transition scene - no callback set!");
		}
	}

	glm::vec2 Scene::GetPhysics2DGravity()
	{
		return s_Gravity;
	}

	void Scene::SetPhysics2DGravity(const glm::vec2& gravity)
	{
		s_Gravity = gravity;

		if (m_PhysicsWorld)
			m_PhysicsWorld->SetGravity(b2Vec2(s_Gravity.x, s_Gravity.y));
		else if (m_PhysicsWorld == nullptr)
			m_PhysicsWorld = new b2World({ s_Gravity.x, s_Gravity.y });
	}

	/*
	void Scene::RenderHoveredEntityOutline(Entity entity, glm::vec4 color)
	{

	}

	void Scene::RenderSelectedEntityOutline(Entity entity, glm::vec4 color)
	{
		if (entity)
		{
			Entity camera = GetPrimaryCameraEntity();

			if (!camera)
				return;

			Renderer2D::BeginScene(*camera.GetComponent<CameraComponent>().Camera.Raw(), camera.GetComponent<TransformComponent>().GetTransform());

			// Calculate z index for translation
			float zIndex = 0.001f;
			glm::vec3 cameraForwardDirection = camera.GetComponent<CameraComponent>().Camera->GetForwardDirection();

			// Calculate z index for translation
			glm::vec3 projectionCollider = cameraForwardDirection * glm::vec3(zIndex, zIndex, zIndex);

			// Hovered entity outline
			auto& tc = entity.GetComponent<TransformComponent>();

			if (entity.HasComponent<SpriteRendererComponent>() ||
				entity.HasComponent<CircleRendererComponent>())
			{
				rtmcpp::Vec3 translation = rtmcpp::Vec3{ tc.Translation.X, tc.Translation.Y, tc.Translation.Z + -projectionCollider.Z };
				rtmcpp::Mat4 rotation = rtmcpp::Mat4Cast(rtmcpp::FromEuler(rtmcpp::Vec3{ tc.Rotation.Y, tc.Rotation.Z, tc.Rotation.X }));

				rtmcpp::Mat4 transform = rtmcpp::Mat4Cast(rtmcpp::Scale(tc.Scale))
					* rotation
					* rtmcpp::Mat4Cast(rtmcpp::Translation(rtmcpp::Vec3{ translation.X, translation.Y, translation.Z }));

				Renderer2D::SetLineWidth(2.0f);
				Renderer2D::DrawRect(transform, color);
			}

			Renderer2D::EndScene();
		}
	}*/

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

	Entity Scene::GetEntityByID(uint64_t id)
	{
		// TODO: Maybe should be assert
		if (this != nullptr && m_EntityIDMap.size() > 0)
		{
			if (m_EntityIDMap.find(id) != m_EntityIDMap.end())
				return { m_EntityIDMap.at(id), this };

		}

		return {};
	}

	Entity Scene::TryGetEntityWithID(uint64_t id) const
	{
		if (const auto iter = m_EntityIDMap.find(id); iter != m_EntityIDMap.end())
			return { iter->second, const_cast<Scene*>(this) };
	}

	Entity Scene::GetEntityByUUID(UUID uuid)
	{
		if (m_EntityIDMap.find(uuid) != m_EntityIDMap.end())
			return { m_EntityIDMap.at(uuid), this };

		return {};
	}

	Entity Scene::GetEntityWithUUID(UUID id) const
	{
		//SE_PROFILE_FUNC();
		SE_CORE_VERIFY(m_EntityIDMap.find(id) != m_EntityIDMap.end(), "Invalid entity ID or entity doesn't exist in scene!");
		return m_EntityIDMap.at(id);
	}

	void Scene::OnPhysics2DStart()
	{
		m_PhysicsWorld = new b2World({ 0.0f, -9.8f });

		auto view = m_Registry.view<TransformComponent, RigidBody2DComponent>();
		for (auto entity : view)
		{
			Entity e{ entity, this };
			auto& transform = e.GetComponent<TransformComponent>();
			auto& rigidBody2D = e.GetComponent<RigidBody2DComponent>();

			b2BodyDef bodyDef;
			switch (rigidBody2D.BodyType)
			{
				case RigidBody2DComponent::Type::Static:    bodyDef.type = b2_staticBody;    break;
				case RigidBody2DComponent::Type::Dynamic:   bodyDef.type = b2_dynamicBody;   break;
				case RigidBody2DComponent::Type::Kinematic: bodyDef.type = b2_kinematicBody; break;
			}

			bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
			bodyDef.angle = transform.GetRotationEuler().z; // if degrees, use glm::radians(...)

			b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
			body->SetFixedRotation(rigidBody2D.FixedRotation);
			body->SetGravityScale(rigidBody2D.GravityScale);
			body->SetLinearDamping(rigidBody2D.LinearDrag);
			body->SetAngularDamping(rigidBody2D.AngularDrag);
			body->SetBullet(rigidBody2D.IsBullet);

			// store entity id in userdata
			const UUID eid = e.GetComponent<IDComponent>().ID;
			body->GetUserData().pointer = static_cast<uintptr_t>(eid);

			rigidBody2D.RuntimeBody = body;

			if (e.HasComponent<BoxCollider2DComponent>())
			{
				// const auto& tc = GetWorldSpaceTransform(e);
				// const auto& bc2d = e.GetComponent<BoxCollider2DComponent>();
				// (create Box2D fixture here)
			}
			if (e.HasComponent<CircleCollider2DComponent>())
			{
				// const auto& tc = GetWorldSpaceTransform(e);
				// const auto& cc2d = e.GetComponent<CircleCollider2DComponent>();
				// (create circle fixture here)
			}
		}
	}


	void Scene::OnPhysics2DStop()
	{
		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
	}

	Entity Scene::TryGetEntityWithUUID(UUID id) const
	{
		//HZ_PROFILE_FUNC();
		if (const auto iter = m_EntityIDMap.find(id); iter != m_EntityIDMap.end())
			return iter->second;
		return Entity{};
	}

	void Scene::RenderScene(EditorCamera& camera)
	{
		Renderer2D::BeginScene(camera);

		// Draw sprites
		{
			auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
			for (auto entity : group)
			{
				auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
				Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
			}
		}

		// Draw circles
		{
			auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
			for (auto entity : view)
			{
				auto [transform, circle] = view.get<TransformComponent, CircleRendererComponent>(entity);
				Renderer2D::DrawCircle(transform.GetTransform(), circle.Color, circle.Thickness, circle.Fade, (int)entity);
			}
		}
		// Draw text
		{
			auto view = m_Registry.view<TransformComponent, TextComponent>();
			for (auto entity : view)
			{
				auto [transform, text] = view.get<TransformComponent, TextComponent>(entity);
				Renderer2D::DrawString(text.TextString, transform.GetTransform(), text, (int)entity);
			}
		}

		Renderer2D::EndScene();
	}

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
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
	{
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
		if (component.Audio && !component.AudioSourceData.UsePlaylist)
		{
			Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(component.Audio);
			if (audioSource != nullptr)
				audioSource->SetConfig(component.Config);
		}
		else if (component.Audio && component.AudioSourceData.UsePlaylist)
		{
			for (auto audio : component.AudioSourceData.Playlist)
			{
				Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(audio);
				if (audioSource != nullptr)
					audioSource->SetConfig(component.Config);
			}
		}
	}

	template<>
	void Scene::OnComponentAdded<AudioListenerComponent>(Entity entity, AudioListenerComponent& component)
	{
		if (component.Listener)
			component.Listener->SetConfig(component.Config);
	}
}
