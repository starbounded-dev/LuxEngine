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
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Physics/ContactListener2D.h"

#include <glm/glm.hpp>

// Box2D
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_circle_shape.h"

namespace Lux {

	static ContactListener2D s_Box2DContactListener;

	Scene::Scene()
	{
		m_Renderer2D = Ref<Renderer2D>::Create();
	}

	Scene::~Scene()
	{
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
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		m_EntityMap[uuid] = entity;
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
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
							Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(ac.Audio);

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
								Ref<AudioSource> playingSourceIndex = AssetManager::GetAsset<AudioSource>(ac.AudioSourceData.Playlist[ac.AudioSourceData.CurrentIndex]);

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
					transform.Rotation.z = body->GetAngle();
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
							Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(asc.Audio);
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

							Ref<AudioSource> oldSource = AssetManager::GetAsset<AudioSource>(playlist[asc.AudioSourceData.OldIndex]);
							Ref<AudioSource> currentSource = AssetManager::GetAsset<AudioSource>(playlist[asc.AudioSourceData.CurrentIndex]);

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

						Entity e = { entity , this};

						if (asc.Audio)
						{
							if (!asc.AudioSourceData.UsePlaylist)
							{
								Ref<AudioSource> audioSource = AssetManager::GetAsset<AudioSource>(asc.Audio);
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
			m_Renderer2D->BeginScene(mainCamera->GetProjectionMatrix()* view, view);

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

					Ref<Font> font = AssetManager::GetAsset<Font>(text.FontHandle);
					if (font)
					{
						m_Renderer2D->DrawString(text.TextString, font, GetWorldSpaceTransformMatrix(Entity{ entity, this }), text.MaxWidth, text.Color, text.LineSpacing, text.Kerning);
					}
				}
			}

			m_Renderer2D->EndScene();
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
					transform.Rotation.z = body->GetAngle();
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
				cameraComponent.Camera.SetOrthographicSize((float)width / (float)height);
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
		// Copy name because we're going to modify component data structure
		std::string name = entity.GetName();
		Entity newEntity = CreateEntity(name);
		CopyComponentIfExists(AllComponents{}, newEntity, entity);
		return newEntity;
	}

	Entity Scene::InstantiatePrefab(Ref<Prefab> prefab)
	{
		if (!prefab)
			return {};

		const Ref<Scene>& prefabScene = prefab->GetScene();
		if (!prefabScene)
			return {};

		Entity prefabRoot = prefabScene->GetEntityByUUID(prefab->GetRootEntityID());
		if (!prefabRoot)
			return {};

		using PrefabInstantiationComponents =
			ComponentGroup<TransformComponent, SpriteRendererComponent, CircleRendererComponent, CameraComponent, ScriptComponent,
			NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent, TextComponent,
			AudioData, AudioSourceComponent, AudioListenerComponent, MeshComponent, MeshTagComponent, StaticMeshComponent,
			DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent>;

		std::function<Entity(Entity, Entity)> instantiateHierarchy;
		instantiateHierarchy = [&](Entity source, Entity parent) -> Entity
		{
			Entity destination = CreateEntity(source.GetName());
			CopyComponentIfExists(PrefabInstantiationComponents{}, destination, source);

			auto& prefabComponent = destination.AddOrReplaceComponent<PrefabComponent>();
			prefabComponent.PrefabID = prefab->Handle;
			prefabComponent.EntityID = source.GetUUID();

			if (parent)
				destination.SetParent(parent);

			if (source.HasComponent<RelationshipComponent>())
			{
				for (const UUID childID : source.Children())
				{
					Entity child = prefabScene->GetEntityByUUID(childID);
					if (child)
						instantiateHierarchy(child, destination);
				}
			}

			return destination;
		};

		return instantiateHierarchy(prefabRoot, {});
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
			bodyDef.type = Utils::RigidBody2DTypeToBox2DBody(rb2d.Type);
			bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
			bodyDef.angle = transform.Rotation.z;

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
				Ref<Font> font = AssetManager::GetAsset<Font>(text.FontHandle);
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
				pl.CastsShadows = pointLight.CastShadows;

				lightEnv.PointLights.push_back(pl);
			}
		}

		return lightEnv;
	}

	Ref<Environment> Scene::CollectEnvironment(float& outIntensity) const
	{
		outIntensity = 1.0f;

		auto view = m_Registry.view<const SkyLightComponent>();
		for (auto entity : view)
		{
			const auto& skyLight = view.get<const SkyLightComponent>(entity);
			
			if (skyLight.EnvironmentMap)
			{
				outIntensity = skyLight.Intensity;
				return AssetManager::GetAsset<Environment>(skyLight.EnvironmentMap);
			}
		}

		return nullptr;
	}

	void Scene::SubmitStaticMeshes(Ref<SceneRenderer> renderer, 
	                               const std::function<bool(Entity)>& isSelected) const
	{
		auto view = m_Registry.view<const TransformComponent, const StaticMeshComponent>();
		for (auto e : view)
		{
			Entity entity = { e, const_cast<Scene*>(this) };
			const auto& meshComp = view.get<const StaticMeshComponent>(e);

			if (!meshComp.Visible)
				continue;

			if (!meshComp.Mesh)
				continue;

			Ref<StaticMesh> staticMesh = AssetManager::GetAsset<StaticMesh>(meshComp.Mesh);
			if (!staticMesh)
				continue;

			AssetHandle meshSourceHandle = staticMesh->GetMeshSource();
			Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(meshSourceHandle);
			if (!meshSource)
				continue;

			Ref<MaterialTable> materialTable = nullptr;
			if (meshComp.MaterialTable)
			{
				auto materialAsset = AssetManager::GetAsset<MaterialAsset>(meshComp.MaterialTable);
				if (materialAsset)
				{
					materialTable = Ref<MaterialTable>::Create(1);
					materialTable->SetMaterial(0, meshComp.MaterialTable);
				}
			}

			// Use the mesh's default material table if none specified
			if (!materialTable)
				materialTable = staticMesh->GetMaterials();

			bool selected = isSelected ? isSelected(entity) : false;

			renderer->SubmitStaticMesh(
				staticMesh,
				meshSource,
				materialTable,
				GetWorldSpaceTransformMatrix(entity),
				nullptr,  // no override material
				selected
			);
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

		// Begin the 3D rendering frame
		renderer->BeginScene(sceneCamera);

		// Collect and set light environment from DirectionalLight and PointLight components
		LightEnvironment lightEnv = CollectLightEnvironment();
		renderer->SetLightEnvironment(lightEnv);

		// Collect and set skybox/IBL environment from SkyLightComponent
		float envIntensity = 1.0f;
		Ref<Environment> environment = CollectEnvironment(envIntensity);
		if (environment)
			renderer->SetEnvironment(environment, envIntensity);

		// Submit all static meshes for rendering
		SubmitStaticMeshes(renderer, isSelected);

		// End the frame and execute the render passes
		renderer->EndScene();
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
		// Note: Near/Far/FOV from runtime camera component if needed

		// Begin the 3D rendering frame
		renderer->BeginScene(sceneCamera);

		// Collect and set light environment
		LightEnvironment lightEnv = CollectLightEnvironment();
		renderer->SetLightEnvironment(lightEnv);

		// Collect and set skybox/IBL environment
		float envIntensity = 1.0f;
		Ref<Environment> environment = CollectEnvironment(envIntensity);
		if (environment)
			renderer->SetEnvironment(environment, envIntensity);

		// Submit all static meshes (no selection highlight in runtime)
		SubmitStaticMeshes(renderer, nullptr);

		// End the frame
		renderer->EndScene();
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
			component.Camera.SetOrthographicSize((float)m_ViewportWidth / (float)m_ViewportHeight);
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
