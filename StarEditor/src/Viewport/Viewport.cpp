#include "sepch.h"﻿
#include "Viewport.h"

#include <imgui/imgui.h>

#include "EditorLayer.h"
//#include "StarEngine/Audio/AudioComponent.h"
#include "StarEngine/Core/Math/Ray.h"
#include "StarEngine/Debug/Profiler.h"
#include "StarEngine/Editor/EditorResources.h"
#include "StarEngine/ImGui/ImGuiEx.h"
#include "StarEngine/ImGui/ImGuizmo.h"
#include "StarEngine/Renderer/Image.h"
#include "StarEngine/Renderer/SceneRenderer.h"

namespace StarEngine
{

	namespace
	{
		template<typename TComponent>
		void DrawBillboardForComponent(Ref<Scene> currentScene, Ref<Renderer2D> renderer2D, const Ref<Texture2D>& billboardTexture, const std::function<void(Entity&)>& func = nullptr)
		{
			auto entities = currentScene->GetAllEntitiesWith<TComponent>();
			for (auto e : entities)
			{
				Entity entity = { e, currentScene.Raw() };
				const bool isSelected = SelectionManager::IsSelected(currentScene->GetUUID(), entity.GetUUID());
				const glm::vec4 color = isSelected ? glm::vec4(1.0f, 0.5f, 0.0f, 1.f) : glm::vec4(1.f);
				renderer2D->DrawQuadBillboard(currentScene->GetWorldSpaceTransform(entity).Translation, { 1.0f, 1.0f }, billboardTexture, 1.f, color);
				if (func != nullptr)
				{
					func(entity);
				}
			}
		}

		bool RayIntersectsBillboard(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float rayDistance, const glm::vec3& billboardPos, float sideLength)
		{
			const float epsilon = 1e-6f;
			float half_S = sideLength / 2.0f;
			glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

			glm::vec3 vecToOrigin = rayOrigin - billboardPos;
			float vecLength = glm::length(vecToOrigin);
			if (vecLength < epsilon)
			{
				return false;
			}
			glm::vec3 N = vecToOrigin / vecLength;

			glm::vec3 crossUp = glm::cross(N, worldUp);
			float crossLength = glm::length(crossUp);
			if (crossLength < epsilon)
			{
				crossUp = glm::cross(N, glm::vec3(1.0f, 0.0f, 0.0f));
				crossLength = glm::length(crossUp);
				if (crossLength < epsilon)
				{
					return false;
				}
			}
			glm::vec3 U = crossUp / crossLength;
			glm::vec3 V = glm::cross(N, U);

			float denom = glm::dot(rayDirection, N);
			if (std::fabs(denom) < epsilon)
			{
				return false;
			}
			float t = glm::dot(billboardPos - rayOrigin, N) / denom;
			if (t < 0.0f || t > rayDistance)
			{
				return false;
			}
			glm::vec3 h = rayOrigin + t * rayDirection;

			glm::vec3 offset = h - billboardPos;
			float localU = glm::dot(offset, U);
			float localV = glm::dot(offset, V);
			if (std::fabs(localU) <= half_S && std::fabs(localV) <= half_S)
			{
				return true;
			}
			return false;
		}

		struct SelectionData
		{
			Entity Entity;
			float Distance = 0.0f;
		};

		template<typename TComponent>
		void TrySelectBillboardEntity(const glm::vec3& rayOrigin, const glm::vec3 rayDirection, Ref<Scene> currentScene, std::vector<SelectionData>& selections)
		{
			auto entities = currentScene->GetAllEntitiesWith<TComponent>();
			for (auto ent : entities)
			{
				Entity entity = { ent, currentScene.Raw() };
				TransformComponent comp = currentScene->GetWorldSpaceTransform(entity);
				if (RayIntersectsBillboard(rayOrigin, rayDirection, std::numeric_limits<float>::max(), comp.Translation, comp.Scale.x))
				{
					SelectionData selection;
					selection.Entity = entity;
					selection.Distance = glm::distance(rayOrigin, comp.Translation);
					selections.emplace_back(selection);
				}
			}
		}
	}

	Viewport::Viewport(const std::string& viewportName, EditorLayer* editorLayer)
		: m_EditorLayer(editorLayer), m_ViewportName(viewportName), m_ViewportCamera(45.0f, 1280.0f, 720.0f, 0.1f, 1000.0f)
	{
	}

	bool Viewport::IsViewportVisible() const
	{
		return m_IsVisible;
	}

	bool Viewport::IsMainViewport() const
	{
		return m_IsMainViewport;
	}

	Ref<SceneRenderer> Viewport::GetRenderer() const
	{
		return m_ViewportRenderer;
	}

	Ref<Renderer2D> Viewport::GetRenderer2D() const
	{
		return m_ViewportRenderer2D;
	}

	EditorCamera& Viewport::GetViewportCamera()
	{
		return m_ViewportCamera;
	}

	const std::string& Viewport::GetName() const
	{
		return m_ViewportName;
	}

	std::array<glm::vec2, 2> Viewport::GetViewportBounds() const
	{
		return m_ViewportBounds;
	}

	bool* Viewport::GetIsVisibleMemory()
	{
		return &m_IsVisible;
	}

	void Viewport::Init(const Ref<Scene>& scene)
	{
		m_ViewportRenderer = Ref<SceneRenderer>::Create(scene);
		m_SelectedCameraRenderer = Ref<SceneRenderer>::Create(scene);
		m_ViewportRenderer2D = Ref<Renderer2D>::Create();

		m_ViewportRenderer->SetLineWidth(m_LineWidth);
		m_SelectedCameraRenderer->SetLineWidth(m_LineWidth);
		m_ViewportRenderer2D->SetLineWidth(m_LineWidth);
	}

	void Viewport::SetIsMainViewport(bool isMain)
	{
		m_IsMainViewport = isMain;
	}

	void Viewport::SetIsVisible(bool visible)
	{
		m_IsVisible = visible;
	}

	void Viewport::OnUpdate(Timestep ts)
	{
		if (m_IsVisible)
		{
			m_ViewportCamera.SetActive(m_IsFocused);
			m_ViewportCamera.OnUpdate(ts);

			m_SelectedCameraEntity = GetSelectedCameraEntity();

			if (m_EditorLayer->m_CurrentScene != m_EditorLayer->m_EditorScene)
			{
				if (!m_IsMainViewport)
				{
					m_EditorLayer->m_CurrentScene->OnRenderEditor(m_ViewportRenderer, m_ViewportCamera);

					if (m_SelectedCameraEntity.IsValid())
					{
						m_EditorLayer->m_CurrentScene->OnRenderCameraEntityRuntime(m_SelectedCameraRenderer, m_SelectedCameraEntity,
							(uint32_t)m_SelectedCameraBounds[0].x, (uint32_t)m_SelectedCameraBounds[0].y, (uint32_t)m_SelectedCameraBounds[1].x, (uint32_t)m_SelectedCameraBounds[1].y);
					}
				}
			}
			else
			{
				m_EditorLayer->m_EditorScene->OnRenderEditor(m_ViewportRenderer, m_ViewportCamera);

				if (m_SelectedCameraEntity.IsValid())
				{
					m_EditorLayer->m_CurrentScene->OnRenderCameraEntityRuntime(m_SelectedCameraRenderer, m_SelectedCameraEntity,
						(uint32_t)m_SelectedCameraBounds[0].x, (uint32_t)m_SelectedCameraBounds[0].y, (uint32_t)m_SelectedCameraBounds[1].x, (uint32_t)m_SelectedCameraBounds[1].y);
				}
			}

			OnRender();
		}
	}

	void Viewport::OnRender()
	{
		OnRender2D();
	}

	void Viewport::OnRender2D()
	{
		if (!m_ViewportRenderer->GetFinalPassImage())
			return;

		m_ViewportRenderer2D->BeginScene(m_ViewportCamera.GetViewProjection(), m_ViewportCamera.GetViewMatrix());
		m_ViewportRenderer2D->SetTargetFramebuffer(m_ViewportRenderer->GetExternalCompositeFramebuffer());

		if (m_DrawOnTopBoundingBoxes && m_ShowBoundingBoxes)
		{
			if (m_ShowBoundingBoxSelectedMeshOnly)
			{
				const auto& selectedEntities = SelectionManager::GetSelections(m_EditorLayer->m_CurrentScene->GetUUID());
				for (const auto& entityID : selectedEntities)
				{
					Entity entity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(entityID);

					if (const auto mc = entity.TryGetComponent<SubmeshComponent>(); mc)
					{
						if (auto mesh = AssetManager::GetAsset<Mesh>(mc->Mesh); mesh)
						{
							if (auto meshSource = AssetManager::GetAsset<MeshSource>(mesh->GetMeshSource()); meshSource)
							{
								if (m_ShowBoundingBoxSubmeshes)
								{
									const auto& submeshIndices = mesh->GetSubmeshes();
									const auto& submeshes = meshSource->GetSubmeshes();

									for (uint32_t submeshIndex : submeshIndices)
									{
										glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
										const AABB& aabb = submeshes[submeshIndex].BoundingBox;
										m_ViewportRenderer2D->DrawAABB(aabb, transform * submeshes[submeshIndex].Transform, glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
									}
								}
								else
								{
									glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
									const AABB& aabb = meshSource->GetBoundingBox();
									m_ViewportRenderer2D->DrawAABB(aabb, transform, glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
								}
							}
						}
					}
					else if (entity.HasComponent<StaticMeshComponent>())
					{
						if (auto mesh = AssetManager::GetAsset<StaticMesh>(entity.GetComponent<StaticMeshComponent>().StaticMesh); mesh)
						{
							if (auto meshSource = AssetManager::GetAsset<MeshSource>(mesh->GetMeshSource()); meshSource)
							{
								if (m_ShowBoundingBoxSubmeshes)
								{
									const auto& submeshIndices = mesh->GetSubmeshes();
									const auto& submeshes = meshSource->GetSubmeshes();
									for (uint32_t submeshIndex : submeshIndices)
									{
										glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
										const AABB& aabb = submeshes[submeshIndex].BoundingBox;
										m_ViewportRenderer2D->DrawAABB(aabb, transform * submeshes[submeshIndex].Transform, glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
									}
								}
								else
								{
									glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
									const AABB& aabb = meshSource->GetBoundingBox();
									m_ViewportRenderer2D->DrawAABB(aabb, transform, glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
								}
							}
						}
					}
				}
			}
			else
			{
				auto dynamicMeshEntities = m_EditorLayer->m_CurrentScene->GetAllEntitiesWith<SubmeshComponent>();
				for (auto e : dynamicMeshEntities)
				{
					Entity entity = { e, m_EditorLayer->m_CurrentScene.Raw() };
					glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
					if (auto mesh = AssetManager::GetAsset<Mesh>(entity.GetComponent<SubmeshComponent>().Mesh); mesh)
					{
						if (auto meshSource = AssetManager::GetAsset<MeshSource>(mesh->GetMeshSource()); meshSource)
						{
							const AABB& aabb = meshSource->GetBoundingBox();
							m_ViewportRenderer2D->DrawAABB(aabb, transform, glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
						}
					}
				}

				auto staticMeshEntities = m_EditorLayer->m_CurrentScene->GetAllEntitiesWith<StaticMeshComponent>();
				for (auto e : staticMeshEntities)
				{
					Entity entity = { e, m_EditorLayer->m_CurrentScene.Raw() };
					glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
					if (auto mesh = AssetManager::GetAsset<StaticMesh>(entity.GetComponent<StaticMeshComponent>().StaticMesh); mesh)
					{
						if (auto meshSource = AssetManager::GetAsset<MeshSource>(mesh->GetMeshSource()); meshSource)
						{
							const AABB& aabb = meshSource->GetBoundingBox();
							m_ViewportRenderer2D->DrawAABB(aabb, transform, glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
						}
					}
				}
			}
		}

		if (m_ShowIcons)
		{
			DrawBillboardForComponent<PointLightComponent>(m_EditorLayer->m_CurrentScene, m_ViewportRenderer2D, EditorResources::PointLightIcon);
			DrawBillboardForComponent<SpotLightComponent>(m_EditorLayer->m_CurrentScene, m_ViewportRenderer2D, EditorResources::SpotLightIcon);
			DrawBillboardForComponent<CameraComponent>(m_EditorLayer->m_CurrentScene, m_ViewportRenderer2D, EditorResources::CameraIcon, [this](Entity& entity) {DrawCameraFrustum(entity); });
			DrawBillboardForComponent<AudioComponent>(m_EditorLayer->m_CurrentScene, m_ViewportRenderer2D, EditorResources::AudioIcon);
		}

		const auto& selectedEntities = SelectionManager::GetSelections(m_EditorLayer->m_CurrentScene->GetUUID());
		for (const auto& entityID : selectedEntities)
		{
			Entity entity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(entityID);

			if (!entity.HasComponent<PointLightComponent>())
				continue;

			const auto& plc = entity.GetComponent<PointLightComponent>();
			glm::vec3 translation = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransform(entity).Translation;
			m_ViewportRenderer2D->DrawCircle(translation, { 0.0f, 0.0f, 0.0f }, plc.Radius, glm::vec4(plc.Radiance, 1.0f));
			m_ViewportRenderer2D->DrawCircle(translation, { glm::radians(90.0f), 0.0f, 0.0f }, plc.Radius, glm::vec4(plc.Radiance, 1.0f));
			m_ViewportRenderer2D->DrawCircle(translation, { 0.0f, glm::radians(90.0f), 0.0f }, plc.Radius, glm::vec4(plc.Radiance, 1.0f));
		}

		m_ViewportRenderer2D->EndScene();
	}

	void Viewport::OnImGuiRender()
	{
		if (!m_IsVisible)
		{
			return;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(640.f, 360.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		if (ImGui::Begin(m_ViewportName.c_str()))
		{
			m_IsMouseOver = ImGui::IsWindowHovered();
			m_IsFocused = ImGui::IsWindowFocused();

			if (m_IsFocused)
			{
				m_EditorLayer->SetMainViewport(m_ViewportName);
			}

			auto windowSize = ImGui::GetWindowSize();
			ImVec2 minBound = ImGui::GetWindowPos();
			ImVec2 maxBound = { minBound.x + windowSize.x, minBound.y + windowSize.y };

			auto viewportOffset = ImGui::GetCursorPos(); // includes tab bar
			minBound.x += viewportOffset.x;
			minBound.y += viewportOffset.y;

			m_ViewportSize = { maxBound.x - minBound.x, maxBound.y - minBound.y };
			if (m_ViewportSize.x > 1 && m_ViewportSize.y > 1)
			{
				m_ViewportRenderer->SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
				m_ViewportCamera.SetViewportBounds((uint32_t)minBound.x, (uint32_t)minBound.y, (uint32_t)maxBound.x, (uint32_t)maxBound.y);

				m_EditorLayer->m_EditorScene->SetViewportBounds((uint32_t)minBound.x, (uint32_t)minBound.y, (uint32_t)maxBound.x, (uint32_t)maxBound.y);
				if (m_EditorLayer->m_RuntimeScene && m_IsMainViewport)
				{
					m_EditorLayer->m_RuntimeScene->SetViewportBounds((uint32_t)minBound.x, (uint32_t)minBound.y, (uint32_t)maxBound.x, (uint32_t)maxBound.y);
				}

				// Render viewport image
				Ref<Image2D> finalImage = m_ViewportRenderer->GetFinalPassImage();
				if (finalImage != nullptr)
				{
					ImGuiEx::Image(m_ViewportRenderer->GetFinalPassImage(), ImVec2(m_ViewportSize.x, m_ViewportSize.y));
					UI_HandleAssetDrop();
				}

				m_ViewportBounds[0] = { minBound.x, minBound.y };
				m_ViewportBounds[1] = { maxBound.x, maxBound.y };

				m_SelectedCameraBounds[0] = m_ViewportBounds[0] * s_SelectedCameraSizePercent;
				m_SelectedCameraBounds[1] = m_ViewportBounds[1] * s_SelectedCameraSizePercent;

				UI_GizmosToolbar();
				UI_CentralToolbar();
				UI_ViewportSettings();

				if (m_IsMouseOver && m_ShowGizmos && (m_ShowGizmosInPlayMode || m_EditorLayer->m_CurrentScene != m_EditorLayer->m_RuntimeScene || !m_IsMainViewport))
					UI_DrawGizmos();

				DrawSelectedCameraPreview();
			}
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	void Viewport::OnEvent(Event& e)
	{
		if (m_IsMouseOver)
		{
			m_ViewportCamera.OnEvent(e);

			EventDispatcher dispatcher(e);
			dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& event) { return OnMouseButtonPressed(event); });
		}

		if (m_IsFocused)
		{
			EventDispatcher dispatcher(e);
			dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event) { return OnKeyPressedEvent(event); });
		}
	}

	void Viewport::OnProjectClosed()
	{
		m_ViewportRenderer->SetScene(nullptr);
		m_SelectedCameraRenderer->SetScene(nullptr);
	}

	void Viewport::OnScenePlay()
	{
		m_SelectedCameraEntity = {};
	}

	void Viewport::OnNewScene(const Ref<Scene>& newScene)
	{
		ResetCamera();

		if (m_ViewportRenderer)
			m_ViewportRenderer->SetScene(newScene);

		if (m_SelectedCameraRenderer)
			m_SelectedCameraRenderer->SetScene(newScene);
	}

	void Viewport::OnOpenScene(const Ref<Scene>& scene)
	{
		if (m_ViewportRenderer)
			m_ViewportRenderer->SetScene(scene);

		if (m_SelectedCameraRenderer)
			m_SelectedCameraRenderer->SetScene(scene);
	}

	void Viewport::ResetCamera()
	{
		m_ViewportCamera = EditorCamera(45.0f, 1280.0f, 720.0f, 0.1f, 1000.0f);
	}

	void Viewport::UI_GizmosToolbar()
	{
		if ((!m_ShowGizmosInPlayMode && m_IsMainViewport) && m_EditorLayer->m_CurrentScene == m_EditorLayer->m_RuntimeScene)
			return;

		ImGuiEx::ScopedStyle disableSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGuiEx::ScopedStyle disableWindowBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGuiEx::ScopedStyle windowRounding(ImGuiStyleVar_WindowRounding, 4.0f);
		ImGuiEx::ScopedStyle disablePadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		const float cursorYOffset = ImGui::GetCursorStartPos().y + 10.f;
		constexpr float desiredHeight = 26.0f;
		constexpr float buttonSize = 18.0f;
		constexpr float edgeOffset = 4.0f;
		constexpr float numberOfButtons = 4.0f;
		constexpr float gizmoPanelBackgroundWidth = edgeOffset * 6.0f + buttonSize * numberOfButtons + edgeOffset * (numberOfButtons - 1.0f) * 2.0f;

		ImGui::SetCursorPos(ImVec2(15.f, cursorYOffset));
		{
			ImVec2 p_min = ImGui::GetCursorScreenPos();
			ImVec2 size = ImVec2(gizmoPanelBackgroundWidth, desiredHeight);
			ImVec2 p_max = ImVec2(p_min.x + size.x, p_min.y + size.y);

			ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(15, 15, 15, 127), 4.0f);

			char verticalName[256];
			snprintf(verticalName, sizeof(verticalName), "%s_%s", "##gizmosV", m_ViewportName.c_str());

			ImGui::BeginVertical(verticalName, { gizmoPanelBackgroundWidth, desiredHeight });
			ImGui::Spring();

			char horizontalName[256];
			snprintf(horizontalName, sizeof(horizontalName), "%s_%s", "##gizmosH", m_ViewportName.c_str());

			ImGui::BeginHorizontal(horizontalName, { gizmoPanelBackgroundWidth, desiredHeight });
			ImGui::Spring();
			{
				ImGuiEx::ScopedStyle enableSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(edgeOffset * 2.0f, 0));

				const ImColor c_SelectedGizmoButtonColor = Colors::Theme::accent;
				const ImColor c_UnselectedGizmoButtonColor = Colors::Theme::textBrighter;

				auto gizmoButton = [&c_SelectedGizmoButtonColor, buttonSize](const Ref<Texture2D>& icon, const ImColor& tint, float paddingY = 0.0f)
					{
						const float height = std::min((float)icon->GetHeight(), buttonSize) - paddingY * 2.0f;
						const float width = (float)icon->GetWidth() / (float)icon->GetHeight() * height;
						const bool clicked = ImGui::InvisibleButton(ImGuiEx::GenerateID(), ImVec2(width, height));
						ImGuiEx::DrawButtonImage(icon,
							tint,
							tint,
							tint,
							ImGuiEx::RectOffset(ImGuiEx::GetItemRect(), 0.0f, paddingY));

						return clicked;
					};

				ImColor buttonTint = m_EditorLayer->m_GizmoType == -1 ? c_SelectedGizmoButtonColor : c_UnselectedGizmoButtonColor;
				if (gizmoButton(EditorResources::PointerIcon, buttonTint))
					m_EditorLayer->m_GizmoType = -1;
				ImGuiEx::SetTooltip("Select");

				buttonTint = m_EditorLayer->m_GizmoType == ImGuizmo::OPERATION::TRANSLATE ? c_SelectedGizmoButtonColor : c_UnselectedGizmoButtonColor;
				if (gizmoButton(EditorResources::MoveIcon, buttonTint))
					m_EditorLayer->m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
				ImGuiEx::SetTooltip("Translate");

				buttonTint = m_EditorLayer->m_GizmoType == ImGuizmo::OPERATION::ROTATE ? c_SelectedGizmoButtonColor : c_UnselectedGizmoButtonColor;
				if (gizmoButton(EditorResources::RotateIcon, buttonTint))
					m_EditorLayer->m_GizmoType = ImGuizmo::OPERATION::ROTATE;
				ImGuiEx::SetTooltip("Rotate");

				buttonTint = m_EditorLayer->m_GizmoType == ImGuizmo::OPERATION::SCALE ? c_SelectedGizmoButtonColor : c_UnselectedGizmoButtonColor;
				if (gizmoButton(EditorResources::ScaleIcon, buttonTint))
					m_EditorLayer->m_GizmoType = ImGuizmo::OPERATION::SCALE;
				ImGuiEx::SetTooltip("Scale");

			}
			ImGui::Spring();
			ImGui::EndHorizontal();
			ImGui::Spring();
			ImGui::EndVertical();
		}

		// Gizmo orientation window
		constexpr float offsetFromLeft = 10.f;
		constexpr float worldLocalBackgroundWidth = edgeOffset * 6.0f + buttonSize + edgeOffset * 2.0f;

		ImGui::SetCursorPos(ImVec2(15.f + gizmoPanelBackgroundWidth + offsetFromLeft, cursorYOffset));

		char worldLocalName[256];
		snprintf(worldLocalName, sizeof(worldLocalName), "%s_%s", "##world_local", m_ViewportName.c_str());
		{

			ImVec2 p_min = ImGui::GetCursorScreenPos();
			ImVec2 size = ImVec2(worldLocalBackgroundWidth, desiredHeight);
			ImVec2 p_max = ImVec2(p_min.x + size.x, p_min.y + size.y);

			ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(15, 15, 15, 127), 4.0f);

			char worldLocalHorizontalName[256];
			snprintf(worldLocalHorizontalName, sizeof(worldLocalHorizontalName), "%s_%s", "##world_localH", m_ViewportName.c_str());

			ImGui::BeginHorizontal(worldLocalHorizontalName, { worldLocalBackgroundWidth, desiredHeight });
			ImGui::Spring();
			{
				auto worldLocalButton = [buttonSize](const Ref<Texture2D>& worldIcon, const Ref<Texture2D>& localIcon, bool isWorld, const ImColor& tint, float paddingY = 0.0f)
					{
						const float iconHeight = isWorld ? static_cast<float>(worldIcon->GetHeight()) : static_cast<float>(localIcon->GetHeight());
						const float iconWidth = isWorld ? static_cast<float>(worldIcon->GetWidth()) : static_cast<float>(localIcon->GetWidth());

						const float height = std::min(iconHeight, buttonSize) - paddingY * 2.0f;
						const float width = iconWidth / iconHeight * height;
						const bool clicked = ImGui::InvisibleButton(ImGuiEx::GenerateID(), ImVec2(width, height));
						ImGuiEx::DrawButtonImage(isWorld ? worldIcon : localIcon, tint, tint, tint,
							ImGuiEx::RectOffset(ImGuiEx::GetItemRect(), 0.0f, paddingY));

						return clicked;
					};

				if (worldLocalButton(EditorResources::GizmoWorldOrientationIcon, EditorResources::GizmoLocalOrientationIcon, m_EditorLayer->m_GizmoWorldOrientation, Colors::Theme::textBrighter))
					m_EditorLayer->m_GizmoWorldOrientation = !m_EditorLayer->m_GizmoWorldOrientation;

				ImGuiEx::SetTooltip("Toggles the transform gizmo coordinate systems between world and local (object) space");
			}
			ImGui::Spring();
			ImGui::EndHorizontal();
		}
	}

	void Viewport::UI_CentralToolbar()
	{
		if (!m_IsMainViewport)
			return;

		ImGuiEx::ScopedStyle disableSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGuiEx::ScopedStyle disableWindowBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGuiEx::ScopedStyle windowRounding(ImGuiStyleVar_WindowRounding, 4.0f);
		ImGuiEx::ScopedStyle disablePadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		const float cursorYOffset = ImGui::GetCursorStartPos().y + 10.f;
		constexpr float buttonSize = 18.0f + 5.0f;
		constexpr float edgeOffset = 4.0f;
		constexpr float numberOfButtons = 3.0f;
		constexpr float desiredHeight = 26.0f + 5.0f;
		constexpr float backgroundWidth = edgeOffset * 6.0f + buttonSize * numberOfButtons + edgeOffset * (numberOfButtons - 1.0f) * 2.0f;

		ImGui::SetCursorPos(ImVec2((ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x) / 2.f - backgroundWidth / 2.f, cursorYOffset));

		ImVec2 p_min = ImGui::GetCursorScreenPos();
		ImVec2 size = ImVec2(backgroundWidth, desiredHeight);
		ImVec2 p_max = ImVec2(p_min.x + size.x, p_min.y + size.y);

		ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(15, 15, 15, 127), 4.0f);

		char verticalName[256];
		snprintf(verticalName, sizeof(verticalName), "%s_%s", "##viewport_central_toolbarV", m_ViewportName.c_str());

		ImGui::BeginVertical(verticalName, { backgroundWidth, desiredHeight });
		ImGui::Spring();

		char horizontalName[256];
		snprintf(horizontalName, sizeof(horizontalName), "%s_%s", "##viewport_central_toolbarH", m_ViewportName.c_str());

		ImGui::BeginHorizontal(horizontalName, { backgroundWidth, desiredHeight });
		ImGui::Spring();
		{
			ImGuiEx::ScopedStyle enableSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(edgeOffset * 2.0f, 0));

			const ImColor c_ButtonTint = Colors::Theme::text;
			const ImColor c_SimulateButtonTint = m_EditorLayer->m_SceneState == EditorLayer::SceneState::Simulate ? ImColor(1.0f, 0.75f, 0.75f, 1.0f) : c_ButtonTint;

			auto toolbarButton = [buttonSize](const Ref<Texture2D>& icon, const ImColor& tint, float paddingY = 0.0f)
				{
					const float height = std::min((float)icon->GetHeight(), buttonSize) - paddingY * 2.0f;
					const float width = (float)icon->GetWidth() / (float)icon->GetHeight() * height;
					const bool clicked = ImGui::InvisibleButton(ImGuiEx::GenerateID(), ImVec2(width, height));
					ImGuiEx::DrawButtonImage(icon,
						tint,
						tint,
						tint,
						ImGuiEx::RectOffset(ImGuiEx::GetItemRect(), 0.0f, paddingY));

					return clicked;
				};

			Ref<Texture2D> buttonTex = m_EditorLayer->m_SceneState == EditorLayer::SceneState::Play ? EditorResources::StopIcon : EditorResources::PlayIcon;
			if (toolbarButton(buttonTex, c_ButtonTint))
			{
				m_EditorLayer->m_TitleBarPreviousColor = m_EditorLayer->m_TitleBarActiveColor;
				if (m_EditorLayer->m_SceneState == EditorLayer::SceneState::Edit)
				{
					m_EditorLayer->m_TitleBarTargetColor = Colors::Theme::titlebarOrange;
					m_EditorLayer->OnScenePlay();
				}
				else if (m_EditorLayer->m_SceneState != EditorLayer::SceneState::Simulate)
				{
					m_EditorLayer->m_TitleBarTargetColor = Colors::Theme::titlebarGreen;
					m_EditorLayer->OnSceneStop();
				}

				m_EditorLayer->m_AnimateTitleBarColor = true;
			}
			ImGuiEx::SetTooltip(m_EditorLayer->m_SceneState == EditorLayer::SceneState::Edit ? "Play" : "Stop");

			if (toolbarButton(EditorResources::SimulateIcon, c_SimulateButtonTint))
			{
				if (m_EditorLayer->m_SceneState == EditorLayer::SceneState::Edit)
					m_EditorLayer->OnSceneStartSimulation();
				else if (m_EditorLayer->m_SceneState == EditorLayer::SceneState::Simulate)
					m_EditorLayer->OnSceneStopSimulation();
			}
			ImGuiEx::SetTooltip(m_EditorLayer->m_SceneState == EditorLayer::SceneState::Simulate ? "Stop" : "Simulate Physics");

			if (toolbarButton(EditorResources::PauseIcon, c_ButtonTint))
			{
				if (m_EditorLayer->m_SceneState == EditorLayer::SceneState::Play)
					m_EditorLayer->m_SceneState = EditorLayer::SceneState::Pause;
				else if (m_EditorLayer->m_SceneState == EditorLayer::SceneState::Pause)
					m_EditorLayer->m_SceneState = EditorLayer::SceneState::Play;
			}
			ImGuiEx::SetTooltip(m_EditorLayer->m_SceneState == EditorLayer::SceneState::Pause ? "Resume" : "Pause");
		}
		ImGui::Spring();
		ImGui::EndHorizontal();
		ImGui::Spring();
		ImGui::EndVertical();
	}

	void Viewport::UI_ViewportSettings()
	{
		ImGuiEx::ScopedStyle disableSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGuiEx::ScopedStyle disableWindowBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGuiEx::ScopedStyle windowRounding(ImGuiStyleVar_WindowRounding, 4.0f);
		ImGuiEx::ScopedStyle disablePadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		const float cursorYOffset = ImGui::GetCursorStartPos().y + 10.f;
		constexpr float buttonSize = 18.0f;
		constexpr float edgeOffset = 2.0f;
		constexpr float windowHeight = 32.0f;
		constexpr float numberOfButtons = 1.0f;
		constexpr float backgroundWidth = edgeOffset * 6.0f + buttonSize * numberOfButtons + edgeOffset * (numberOfButtons - 1.0f) * 2.0f;
		constexpr float desiredHeight = 26.0f;

		ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionAvail().x - 15.f - backgroundWidth, cursorYOffset));

		ImVec2 p_min = ImGui::GetCursorScreenPos();
		ImVec2 size = ImVec2(backgroundWidth, desiredHeight);
		ImVec2 p_max = ImVec2(p_min.x + size.x, p_min.y + size.y);

		ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(15, 15, 15, 127), 4.0f);

		bool openSettingsPopup = false;

		char verticalName[256];
		snprintf(verticalName, sizeof(verticalName), "%s_%s", "##viewportSettingsV", m_ViewportName.c_str());

		char horizontalName[256];
		snprintf(horizontalName, sizeof(horizontalName), "%s_%s", "##viewportSettingsH", m_ViewportName.c_str());

		ImGui::BeginVertical(verticalName, { backgroundWidth, desiredHeight });
		ImGui::Spring();
		ImGui::BeginHorizontal(horizontalName, { backgroundWidth, desiredHeight });
		ImGui::Spring();
		{
			ImGuiEx::ScopedStyle enableSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(edgeOffset * 2.0f, 0));
			const ImColor c_UnselectedGizmoButtonColor = Colors::Theme::textBrighter;

			auto imageButton = [buttonSize](const Ref<Texture2D>& icon, const ImColor& tint, float paddingY = 0.0f)
				{
					const float height = std::min((float)icon->GetHeight(), buttonSize) - paddingY * 2.0f;
					const float width = (float)icon->GetWidth() / (float)icon->GetHeight() * height;
					const bool clicked = ImGui::InvisibleButton(ImGuiEx::GenerateID(), ImVec2(width, height));
					ImGuiEx::DrawButtonImage(icon,
						tint,
						tint,
						tint,
						ImGuiEx::RectOffset(ImGuiEx::GetItemRect(), 0.0f, paddingY));

					return clicked;
				};

			if (imageButton(EditorResources::GearIcon, c_UnselectedGizmoButtonColor))
				openSettingsPopup = true;
			ImGuiEx::SetTooltip("Viewport Settings");
		}
		ImGui::Spring();
		ImGui::EndHorizontal();
		ImGui::Spring();
		ImGui::EndVertical();

		// Draw the settings popup
		{
			int32_t sectionIdx = 0;

			static float popupWidth = 310.0f;

			auto beginSection = [&sectionIdx](const char* name)
				{
					if (sectionIdx > 0)
						ImGuiEx::ShiftCursorY(5.5f);

					ImGuiEx::Fonts::PushFont("Bold");
					ImGui::TextUnformatted(name);
					ImGuiEx::Fonts::PopFont();
					ImGuiEx::Draw::Underline(Colors::Theme::backgroundDark);
					ImGuiEx::ShiftCursorY(3.5f);

					bool result = ImGui::BeginTable("##section_table", 2, ImGuiTableFlags_SizingStretchSame);
					if (result)
					{
						ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthFixed, popupWidth * 0.5f);
						ImGui::TableSetupColumn("Widgets", ImGuiTableColumnFlags_WidthFixed, popupWidth * 0.5f);
					}

					sectionIdx++;
					return result;
				};

			auto endSection = []()
				{
					ImGui::EndTable();
				};

			auto slider = [](const char* label, float& value, float min = 0.0f, float max = 0.0f)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1);
					ImGuiEx::ShiftCursor(GImGui->Style.FramePadding.x, -GImGui->Style.FramePadding.y);
					return ImGuiEx::SliderFloat(ImGuiEx::GenerateID(), &value, min, max);
				};

			auto drag = [](const char* label, float& value, float delta = 1.0f, float min = 0.0f, float max = 0.0f)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1);
					ImGuiEx::ShiftCursor(GImGui->Style.FramePadding.x, -GImGui->Style.FramePadding.y);
					return ImGuiEx::DragFloat(ImGuiEx::GenerateID(), &value, delta, min, max);
				};

			auto checkbox = [](const char* label, bool& value)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					auto table = ImGui::GetCurrentTable();
					float columnWidth = table->Columns[1].WidthMax;
					ImGuiEx::ShiftCursor(columnWidth - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x, -GImGui->Style.FramePadding.y);
					return ImGuiEx::Checkbox(ImGuiEx::GenerateID(), &value);
				};

			auto dropdown = [](const char* label, const char** options, int32_t optionCount, int32_t* selected)
				{
					const char* current = options[*selected];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::PushItemWidth(-1);

					bool result = false;
					ImGuiEx::ShiftCursor(GImGui->Style.FramePadding.x, -GImGui->Style.FramePadding.y);
					if (ImGuiEx::BeginCombo(ImGuiEx::GenerateID(), current))
					{
						for (int i = 0; i < optionCount; i++)
						{
							const bool is_selected = (current == options[i]);
							if (ImGui::Selectable(options[i], is_selected))
							{
								current = options[i];
								*selected = i;
								result = true;
							}

							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGuiEx::EndCombo();
					}
					ImGui::PopItemWidth();

					return result;
				};

			ImGuiEx::ScopedStyle itemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 5.5f));
			ImGuiEx::ScopedStyle windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
			ImGuiEx::ScopedStyle windowRounding(ImGuiStyleVar_PopupRounding, 4.0f);
			ImGuiEx::ScopedStyle cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 5.5f));

			if (openSettingsPopup)
				ImGui::OpenPopup("ViewportSettingsPanel");

			ImGui::SetNextWindowPos({ (m_ViewportBounds[1].x - popupWidth) - 34, m_ViewportBounds[0].y + edgeOffset + windowHeight });
			if (ImGui::BeginPopup("ViewportSettingsPanel", ImGuiWindowFlags_NoMove))
			{
				auto& viewportRenderOptions = m_ViewportRenderer->GetOptions();

				if (beginSection("General"))
				{
					static const char* selectionModes[] = { "Entity", "Submesh" };
					dropdown("Selection Mode", selectionModes, 2, (int32_t*)&m_SelectionMode);

					static const char* s_TransformTargetNames[] = { "Median Point", "Individual Origins" };
					dropdown("Multi-Transform Target", s_TransformTargetNames, 2, (int32_t*)&m_MultiTransformTarget);

					endSection();
				}

				if (beginSection("Display"))
				{
					checkbox("Show Icons", m_ShowIcons);
					checkbox("Show Gizmos", m_ShowGizmos);
					checkbox("Show Gizmos In Play Mode", m_ShowGizmosInPlayMode);
					checkbox("Show Bounding Boxes", m_ShowBoundingBoxes);
					if (m_ShowBoundingBoxes)
					{
						checkbox("Selected Entity", m_ShowBoundingBoxSelectedMeshOnly);

						if (m_ShowBoundingBoxSelectedMeshOnly)
							checkbox("Submeshes", m_ShowBoundingBoxSubmeshes);
					}

					checkbox("Show Grid", viewportRenderOptions.ShowGrid);
					checkbox("Show Selected Wireframe", viewportRenderOptions.ShowSelectedInWireframe);

					checkbox("Show Animation Debug", viewportRenderOptions.ShowAnimationDebug);

					static const char* physicsColliderViewOptions[] = { "Selected Entity", "All" };
					checkbox("Show Physics Colliders", viewportRenderOptions.ShowPhysicsColliders);
					dropdown("Physics Collider Mode", physicsColliderViewOptions, 2, (int32_t*)&viewportRenderOptions.PhysicsColliderMode);
					checkbox("Show Colliders On Top", viewportRenderOptions.ShowPhysicsCollidersOnTop);

					if (drag("Line Width", m_LineWidth, 0.5f, 1.0f, 10.0f))
					{
						m_ViewportRenderer2D->SetLineWidth(m_LineWidth);
						m_ViewportRenderer->SetLineWidth(m_LineWidth);
					}

					endSection();
				}

				if (beginSection("Scene Camera"))
				{
					slider("Exposure", m_ViewportCamera.GetExposure(), 0.0f, 5.0f);
					drag("Speed", m_ViewportCamera.m_NormalSpeed, 0.001f, 0.0002f, 0.5f);

					endSection();
				}

				ImGui::EndPopup();
			}
		}
	}

	void Viewport::UI_HandleAssetDrop()
	{
		if (!ImGui::BeginDragDropTarget() || m_EditorLayer->m_SceneState != EditorLayer::SceneState::Edit)
			return;

		auto data = ImGui::AcceptDragDropPayload("asset_payload");
		if (data)
		{
			uint64_t count = data->DataSize / sizeof(AssetHandle);

			for (uint64_t i = 0; i < count; i++)
			{
				AssetHandle assetHandle = *(((AssetHandle*)data->Data) + i);
				const AssetMetadata& assetData = Project::GetEditorAssetManager()->GetMetadata(assetHandle);

				// We can't really support dragging and dropping scenes when we're dropping multiple assets
				if (count == 1 && assetData.Type == AssetType::Scene)
				{
					m_EditorLayer->OpenScene(Project::GetEditorAssetManager()->GetFileSystemPath(assetData));
					break;
				}

				Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
				if (asset)
				{
					if (asset->GetAssetType() == AssetType::MeshSource)
					{
						m_EditorLayer->OnCreateMeshFromMeshSource({}, asset.As<MeshSource>());
					}
					else if (asset->GetAssetType() == AssetType::Mesh)
					{
						auto mesh = asset.As<Mesh>();
						auto rootEntity = m_EditorLayer->m_EditorScene->InstantiateMesh(mesh);
						SelectionManager::DeselectAll();
						SelectionManager::Select(m_EditorLayer->m_EditorScene->GetUUID(), rootEntity.GetUUID());
					}
					else if (asset->GetAssetType() == AssetType::StaticMesh)
					{
						auto staticMesh = asset.As<StaticMesh>();
						auto rootEntity = m_EditorLayer->m_EditorScene->InstantiateStaticMesh(staticMesh);
						SelectionManager::DeselectAll();
						SelectionManager::Select(m_EditorLayer->m_EditorScene->GetUUID(), rootEntity.GetUUID());
					}
					else if (asset->GetAssetType() == AssetType::Prefab)
					{
						Ref<Prefab> prefab = asset.As<Prefab>();
						auto rootEntity = m_EditorLayer->m_EditorScene->Instantiate(prefab);
						SelectionManager::DeselectAll();
						SelectionManager::Select(m_EditorLayer->m_EditorScene->GetUUID(), rootEntity.GetUUID());
					}
				}
				else
				{
					m_EditorLayer->m_InvalidAssetMetadataPopupData.Metadata = assetData;
					m_EditorLayer->UI_ShowInvalidAssetMetadataPopup();
				}
			}
		}

		ImGui::EndDragDropTarget();
	}

	Entity Viewport::GetSelectedCameraEntity()
	{
		const auto& selections = SelectionManager::GetSelections(m_EditorLayer->m_CurrentScene->GetUUID());
		for (auto id : selections)
		{
			Entity entity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(id);
			CameraComponent* cameraComponent = entity.TryGetComponent<CameraComponent>();
			if (cameraComponent != nullptr)
			{
				return entity;
				break;
			}
		}

		return {};
	}

	void Viewport::DrawCameraFrustum(Entity& entity)
	{
		CameraComponent& cameraComp = entity.GetComponent<CameraComponent>();
		TransformComponent camTransform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransform(entity);

		const glm::vec3 camPos = camTransform.Translation;
		const glm::quat camRot = camTransform.GetRotation();

		const glm::vec3 forward = camRot * glm::vec3(0.0f, 0.0f, -1.0f);
		const glm::vec3 up = camRot * glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 right = camRot * glm::vec3(1.0f, 0.0f, 0.0f);

		SceneCamera::ProjectionType projectionType = cameraComp.Camera.GetProjectionType();
		if (projectionType == SceneCamera::ProjectionType::Perspective)
		{
			const float normalized = glm::clamp(1.0f - (cameraComp.Camera.GetDegPerspectiveVerticalFOV() / 180.0f), 0.0f, 1.0f);
			const float dist = glm::mix(0.0f, 2.5f, normalized);
			const float size = 2.0f * (1.0f - normalized);

			const glm::vec3 center = camPos + forward * dist;
			const glm::vec3 topLeft = (center + up * size - right * size * 1.5f);
			const glm::vec3 topRight = (center + up * size + right * size * 1.5f);
			const glm::vec3 bottomLeft = center - up * size - right * size * 1.5f;
			const glm::vec3 bottomRight = center - up * size + right * size * 1.5f;

			m_ViewportRenderer2D->DrawLine(camPos, topLeft);
			m_ViewportRenderer2D->DrawLine(camPos, topRight);
			m_ViewportRenderer2D->DrawLine(camPos, bottomLeft);
			m_ViewportRenderer2D->DrawLine(camPos, bottomRight);

			m_ViewportRenderer2D->DrawLine(topLeft, topRight);
			m_ViewportRenderer2D->DrawLine(topLeft, bottomLeft);
			m_ViewportRenderer2D->DrawLine(bottomLeft, bottomRight);
			m_ViewportRenderer2D->DrawLine(topRight, bottomRight);
		}
		else if (projectionType == SceneCamera::ProjectionType::Orthographic)
		{
			const float orthographicSize = cameraComp.Camera.GetOrthographicSize();

			const glm::vec3 nearTopLeft = camPos + up * orthographicSize - right * orthographicSize * 2.0f;
			const glm::vec3 nearTopRight = camPos + up * orthographicSize + right * orthographicSize * 2.0f;
			const glm::vec3 nearBottomLeft = camPos - up * orthographicSize - right * orthographicSize * 2.0f;
			const glm::vec3 nearBottomRight = camPos - up * orthographicSize + right * orthographicSize * 2.0f;

			m_ViewportRenderer2D->DrawLine(nearTopLeft, nearTopRight);
			m_ViewportRenderer2D->DrawLine(nearTopLeft, nearBottomLeft);
			m_ViewportRenderer2D->DrawLine(nearBottomLeft, nearBottomRight);
			m_ViewportRenderer2D->DrawLine(nearTopRight, nearBottomRight);

			const glm::vec3 center = camPos + forward;
			const glm::vec3 topLeft = center + up * orthographicSize - right * orthographicSize * 2.0f;
			const glm::vec3 topRight = center + up * orthographicSize + right * orthographicSize * 2.0f;
			const glm::vec3 bottomLeft = center - up * orthographicSize - right * orthographicSize * 2.0f;
			const glm::vec3 bottomRight = center - up * orthographicSize + right * orthographicSize * 2.0f;

			m_ViewportRenderer2D->DrawLine(topLeft, topRight);
			m_ViewportRenderer2D->DrawLine(topLeft, bottomLeft);
			m_ViewportRenderer2D->DrawLine(bottomLeft, bottomRight);
			m_ViewportRenderer2D->DrawLine(topRight, bottomRight);

			m_ViewportRenderer2D->DrawLine(nearTopLeft, topLeft);
			m_ViewportRenderer2D->DrawLine(nearTopRight, topRight);
			m_ViewportRenderer2D->DrawLine(nearBottomLeft, bottomLeft);
			m_ViewportRenderer2D->DrawLine(nearBottomRight, bottomRight);
		}
	}

	void Viewport::DrawSelectedCameraPreview()
	{
		glm::vec2 previewSize = m_ViewportSize * s_SelectedCameraSizePercent;

		if (m_SelectedCameraEntity.IsValid())
		{
			m_SelectedCameraRenderer->SetViewportSize((uint32_t)previewSize.x, (uint32_t)previewSize.y);
			Ref<Image2D> finalImage = m_SelectedCameraRenderer->GetFinalPassImage();
			if (finalImage != nullptr)
			{
				const float yCursorOffset = ImGui::GetCursorStartPos().y;
				const ImVec2 avail = ImVec2(ImGui::GetContentRegionAvail().x, m_ViewportSize.y + yCursorOffset);
				const ImVec2 imagePos = ImVec2(avail.x - 15.f - previewSize.x, avail.y - 15.f - previewSize.y);

				ImGui::SetCursorPos(imagePos);
				ImGuiEx::Image(m_SelectedCameraRenderer->GetFinalPassImage(), ImVec2(previewSize.x, previewSize.y), { 0, 0 }, { 1, 1 }, ImVec4(1.0, 1.0, 1.0, 1.0), ImVec4(0.0, 0.0, 0.0, 255.0));
			}
		}
	}

	void Viewport::UI_DrawGizmos()
	{
		HZ_PROFILE_FUNC();

		if (m_SelectionMode != SelectionMode::Entity || m_EditorLayer->m_GizmoType == -1)
			return;

		const auto& selections = SelectionManager::GetSelections(m_EditorLayer->m_CurrentScene->GetUUID());

		if (selections.empty())
			return;

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

		bool snap = Input::IsKeyDown(HZ_KEY_LEFT_CONTROL);

		float snapValue = m_EditorLayer->GetSnapValue();
		float snapValues[3] = { snapValue, snapValue, snapValue };

		glm::mat4 projectionMatrix, viewMatrix;
		if (m_IsMainViewport && m_EditorLayer->m_SceneState == EditorLayer::SceneState::Play && !m_EditorLayer->m_EditorCameraInRuntime)
		{
			Entity cameraEntity = m_EditorLayer->m_CurrentScene->GetMainCameraEntity();
			SceneCamera& camera = cameraEntity.GetComponent<CameraComponent>();
			projectionMatrix = camera.GetProjectionMatrix();
			viewMatrix = glm::inverse(m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(cameraEntity));
		}
		else
		{
			projectionMatrix = m_ViewportCamera.GetProjectionMatrix();
			viewMatrix = m_ViewportCamera.GetViewMatrix();
		}

		if (selections.size() == 1)
		{
			Entity entity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(selections[0]);
			TransformComponent& entityTransform = entity.Transform();

			glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);

			HZ_EDITOR_OP_DECLARE;
			if (ImGuizmo::Manipulate(
				glm::value_ptr(viewMatrix),
				glm::value_ptr(projectionMatrix),
				static_cast<ImGuizmo::OPERATION>(m_EditorLayer->m_GizmoType),
				m_EditorLayer->m_GizmoWorldOrientation ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
				glm::value_ptr(transform),
				nullptr,
				snap ? snapValues : nullptr)
				)
			{
				auto hold = entityTransform;

				Entity parent = m_EditorLayer->m_CurrentScene->TryGetEntityWithUUID(entity.GetParentUUID());
				if (parent)
				{
					glm::mat4 parentTransform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(parent);
					transform = glm::inverse(parentTransform) * transform;
				}

				// Manipulated transform is now in local space of parent (= world space if no parent)
				// We can decompose into translation, rotation, and scale and compare with original
				// to figure out how to best update entity transform
				//
				// Why do we do this instead of just setting the entire entity transform?
				// Because it's more robust to set only those components of transform
				// that we are meant to be changing (dictated by m_GizmoType).  That way we avoid
				// small drift (particularly in rotation and scale) due numerical precision issues
				// from all those matrix operations.
				glm::vec3 translation;
				glm::quat rotation;
				glm::vec3 scale;
				Math::DecomposeTransform(transform, translation, rotation, scale);

				switch (m_EditorLayer->m_GizmoType)
				{
				case ImGuizmo::TRANSLATE:
				{
					entityTransform.Translation = translation;
					break;
				}
				case ImGuizmo::ROTATE:
				{
					// Do this in Euler in an attempt to preserve any full revolutions (> 360)
					glm::vec3 originalRotationEuler = entityTransform.GetRotationEuler();

					// Map original rotation to range [-180, 180] which is what ImGuizmo gives us
					originalRotationEuler.x = fmodf(originalRotationEuler.x + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
					originalRotationEuler.y = fmodf(originalRotationEuler.y + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
					originalRotationEuler.z = fmodf(originalRotationEuler.z + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();

					glm::vec3 deltaRotationEuler = glm::eulerAngles(rotation) - originalRotationEuler;

					// Try to avoid drift due numeric precision
					if (fabs(deltaRotationEuler.x) < 0.001) deltaRotationEuler.x = 0.0f;
					if (fabs(deltaRotationEuler.y) < 0.001) deltaRotationEuler.y = 0.0f;
					if (fabs(deltaRotationEuler.z) < 0.001) deltaRotationEuler.z = 0.0f;

					entityTransform.SetRotationEuler(entityTransform.GetRotationEuler() += deltaRotationEuler);
					break;
				}
				case ImGuizmo::SCALE:
				{
					entityTransform.Scale = scale;
					break;
				}
				}

				HZ_EDITOR_AGGLOMERATE_OP(PushComponent<decltype(hold)>, entity, hold);
			}
			else HZ_EDITOR_COMPLETE_OP;
		}
		else
		{
			if (m_MultiTransformTarget == TransformationTarget::MedianPoint && m_EditorLayer->m_GizmoType == ImGuizmo::SCALE)
			{
				// TODO(Peter): Disabling multi-entity scaling for median point mode for now since it causes strange scaling behavior
				return;
			}

			glm::vec3 medianLocation = glm::vec3(0.0f);
			glm::vec3 medianScale = glm::vec3(1.0f);
			glm::vec3 medianRotation = glm::vec3(0.0f);
			for (auto entityID : selections)
			{
				Entity entity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(entityID);
				medianLocation += entity.Transform().Translation;
				medianScale += entity.Transform().Scale;
				medianRotation += entity.Transform().GetRotationEuler();
			}
			medianLocation /= selections.size();
			medianScale /= selections.size();
			medianRotation /= selections.size();

			glm::mat4 medianPointMatrix = glm::translate(glm::mat4(1.0f), medianLocation)
				* glm::toMat4(glm::quat(medianRotation))
				* glm::scale(glm::mat4(1.0f), medianScale);

			glm::mat4 deltaMatrix = glm::mat4(1.0f);

			// TODO(Emily): Undo stack multi-transform.
			if (ImGuizmo::Manipulate(
				glm::value_ptr(viewMatrix),
				glm::value_ptr(projectionMatrix),
				static_cast<ImGuizmo::OPERATION>(m_EditorLayer->m_GizmoType),
				m_EditorLayer->m_GizmoWorldOrientation ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
				glm::value_ptr(medianPointMatrix),
				glm::value_ptr(deltaMatrix),
				snap ? snapValues : nullptr)
				)
			{
				switch (m_MultiTransformTarget)
				{
				case TransformationTarget::MedianPoint:
				{
					for (auto entityID : selections)
					{
						Entity entity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(entityID);
						TransformComponent& transform = entity.Transform();
						transform.SetTransform(deltaMatrix * transform.GetTransform());
					}

					break;
				}
				case TransformationTarget::IndividualOrigins:
				{
					glm::vec3 deltaTranslation, deltaScale;
					glm::quat deltaRotation;
					Math::DecomposeTransform(deltaMatrix, deltaTranslation, deltaRotation, deltaScale);

					for (auto entityID : selections)
					{
						Entity entity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(entityID);
						TransformComponent& transform = entity.Transform();

						switch (m_EditorLayer->m_GizmoType)
						{
						case ImGuizmo::TRANSLATE:
						{
							transform.Translation += deltaTranslation;
							break;
						}
						case ImGuizmo::ROTATE:
						{
							transform.SetRotationEuler(transform.GetRotationEuler() + glm::eulerAngles(deltaRotation));
							break;
						}
						case ImGuizmo::SCALE:
						{
							if (deltaScale != glm::vec3(1.0f, 1.0f, 1.0f))
								transform.Scale *= deltaScale;
							break;
						}
						}
					}
					break;
				}
				}
			}
		}
	}

	bool Viewport::OnKeyPressedEvent(KeyPressedEvent& e)
	{
		if (m_EditorLayer->m_CurrentScene == m_EditorLayer->m_RuntimeScene && IsMainViewport())
			return false;

		if ((m_IsMouseOver) && !Input::IsMouseButtonDown(MouseButton::Right))
		{
			switch (e.GetKeyCode())
			{
			case KeyCode::Q:
				m_EditorLayer->m_GizmoType = -1;
				break;
			case KeyCode::W:
				m_EditorLayer->m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
				break;
			case KeyCode::E:
				m_EditorLayer->m_GizmoType = ImGuizmo::OPERATION::ROTATE;
				break;
			case KeyCode::R:
				m_EditorLayer->m_GizmoType = ImGuizmo::OPERATION::SCALE;
				break;
			case KeyCode::F:
			{
				if (SelectionManager::GetSelectionCount(m_EditorLayer->m_CurrentScene->GetUUID()) == 0)
					break;

				// TODO(Peter): Maybe compute average location to focus on? Or maybe cycle through all the selected entities?
				UUID selectedEntityID = SelectionManager::GetSelections(m_EditorLayer->m_CurrentScene->GetUUID()).front();
				Entity selectedEntity = m_EditorLayer->m_CurrentScene->GetEntityWithUUID(selectedEntityID);
				m_ViewportCamera.Focus(m_EditorLayer->m_CurrentScene->GetWorldSpaceTransform(selectedEntity).Translation);
				break;
			}
			}
		}

		if (Input::IsKeyDown(HZ_KEY_LEFT_CONTROL) && !Input::IsMouseButtonDown(MouseButton::Right))
		{
			switch (e.GetKeyCode())
			{
			case KeyCode::B:
				// Toggle bounding boxes
				m_ShowBoundingBoxes = !m_ShowBoundingBoxes;
				break;
			case KeyCode::G:
				// Toggle grid
				m_ViewportRenderer->GetOptions().ShowGrid = !m_ViewportRenderer->GetOptions().ShowGrid;
				break;
			case KeyCode::P:
			{
				auto& viewportRendererOptions = m_ViewportRenderer->GetOptions();
				viewportRendererOptions.ShowPhysicsColliders = !viewportRendererOptions.ShowPhysicsColliders;
				break;
			}
			}
		}

		return false;
	}

	bool Viewport::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		if (!m_IsMouseOver)
			return false;

		if (e.GetMouseButton() != MouseButton::Left)
			return false;

		if (Input::IsKeyDown(KeyCode::LeftAlt) || Input::IsMouseButtonDown(MouseButton::Right))
			return false;

		const auto& selections = SelectionManager::GetSelections(m_EditorLayer->m_CurrentScene->GetUUID());
		if (ImGuizmo::IsOver() && !selections.empty())
			return false;

		if (ImGui::IsAnyItemHovered())
			return false;

		ImGui::ClearActiveID();

		std::vector<SelectionData> viewportSelections;
		auto [mouseX, mouseY] = GetMouseViewportSpace(m_IsMouseOver);
		if (mouseX > -1.0f && mouseX < 1.0f && mouseY > -1.0f && mouseY < 1.0f)
		{
			auto [origin, direction] = CastRay(mouseX, mouseY);

			auto meshEntities = m_EditorLayer->m_CurrentScene->GetAllEntitiesWith<SubmeshComponent>();
			for (auto e : meshEntities)
			{
				Entity entity = { e, m_EditorLayer->m_CurrentScene.Raw() };
				auto& mc = entity.GetComponent<SubmeshComponent>();
				if (auto mesh = AssetManager::GetAsset<Mesh>(mc.Mesh); mesh)
				{
					if (auto meshSource = AssetManager::GetAsset<MeshSource>(mesh->GetMeshSource()); meshSource)
					{
						auto& submeshes = meshSource->GetSubmeshes();
						auto& submesh = submeshes[mc.SubmeshIndex];
						glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
						Ray ray = {
							glm::inverse(transform) * glm::vec4(origin, 1.0f),
							glm::inverse(glm::mat3(transform)) * direction
						};

						float t;
						bool intersects = ray.IntersectsAABB(submesh.BoundingBox, t);
						if (intersects)
						{
							const auto& triangleCache = meshSource->GetTriangleCache(mc.SubmeshIndex);
							for (const auto& triangle : triangleCache)
							{
								if (ray.IntersectsTriangle(triangle.V0.Position, triangle.V1.Position, triangle.V2.Position, t))
								{
									viewportSelections.push_back({ entity, t });
									break;
								}
							}
						}
					}
				}
			}

			auto staticMeshEntities = m_EditorLayer->m_CurrentScene->GetAllEntitiesWith<StaticMeshComponent>();
			for (auto e : staticMeshEntities)
			{
				Entity entity = { e, m_EditorLayer->m_CurrentScene.Raw() };
				auto& smc = entity.GetComponent<StaticMeshComponent>();
				if (auto staticMesh = AssetManager::GetAsset<StaticMesh>(smc.StaticMesh); staticMesh)
				{
					if (auto meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource()); meshSource)
					{
						auto& submeshes = meshSource->GetSubmeshes();
						for (uint32_t i = 0; i < submeshes.size(); i++)
						{
							auto& submesh = submeshes[i];
							glm::mat4 transform = m_EditorLayer->m_CurrentScene->GetWorldSpaceTransformMatrix(entity);
							Ray ray = {
								glm::inverse(transform * submesh.Transform) * glm::vec4(origin, 1.0f),
								glm::inverse(glm::mat3(transform * submesh.Transform)) * direction
							};

							float t;
							bool intersects = ray.IntersectsAABB(submesh.BoundingBox, t);
							if (intersects)
							{
								const auto& triangleCache = meshSource->GetTriangleCache(i);
								for (const auto& triangle : triangleCache)
								{
									if (ray.IntersectsTriangle(triangle.V0.Position, triangle.V1.Position, triangle.V2.Position, t))
									{
										viewportSelections.push_back({ entity, t });
										break;
									}
								}
							}
						}
					}
				}
			}

			bool ctrlDown = Input::IsKeyDown(KeyCode::LeftControl) || Input::IsKeyDown(KeyCode::RightControl);
			bool shiftDown = Input::IsKeyDown(KeyCode::LeftShift) || Input::IsKeyDown(KeyCode::RightShift);
			if (!ctrlDown)
				SelectionManager::DeselectAll();

			// Billboard selection
			TrySelectBillboardEntity<PointLightComponent>(origin, direction, m_EditorLayer->m_CurrentScene, viewportSelections);
			TrySelectBillboardEntity<SpotLightComponent>(origin, direction, m_EditorLayer->m_CurrentScene, viewportSelections);
			TrySelectBillboardEntity<CameraComponent>(origin, direction, m_EditorLayer->m_CurrentScene, viewportSelections);
			TrySelectBillboardEntity<AudioComponent>(origin, direction, m_EditorLayer->m_CurrentScene, viewportSelections);
			TrySelectBillboardEntity<SpriteRendererComponent>(origin, direction, m_EditorLayer->m_CurrentScene, viewportSelections);

			std::sort(viewportSelections.begin(), viewportSelections.end(), [](auto& a, auto& b) { return a.Distance < b.Distance; });
			if (!viewportSelections.empty())
			{
				Entity entity = viewportSelections.front().Entity;
				if (shiftDown)
				{
					while (entity.GetParent())
					{
						entity = entity.GetParent();
					}
				}
				if (SelectionManager::IsSelected(m_EditorLayer->m_CurrentScene->GetUUID(), entity.GetUUID()) && ctrlDown)
					SelectionManager::Deselect(m_EditorLayer->m_CurrentScene->GetUUID(), entity.GetUUID());
				else
					SelectionManager::Select(m_EditorLayer->m_CurrentScene->GetUUID(), entity.GetUUID());
			}
		}

		return false;
	}

	std::pair<float, float> Viewport::GetMouseViewportSpace(bool primaryViewport) const
	{
		auto [mx, my] = ImGui::GetMousePos();
		const auto& viewportBounds = m_ViewportBounds;
		mx -= viewportBounds[0].x;
		my -= viewportBounds[0].y;
		auto viewportWidth = viewportBounds[1].x - viewportBounds[0].x;
		auto viewportHeight = viewportBounds[1].y - viewportBounds[0].y;

		return { (mx / viewportWidth) * 2.0f - 1.0f, ((my / viewportHeight) * 2.0f - 1.0f) * -1.0f };
	}

	std::pair<glm::vec3, glm::vec3> Viewport::CastRay(float mx, float my) const
	{
		glm::vec4 mouseClipPos = { mx, my, -1.0f, 1.0f };

		auto inverseProj = glm::inverse(m_ViewportCamera.GetProjectionMatrix());
		auto inverseView = glm::inverse(glm::mat3(m_ViewportCamera.GetViewMatrix()));

		glm::vec4 ray = inverseProj * mouseClipPos;
		glm::vec3 rayPos = m_ViewportCamera.GetPosition();
		glm::vec3 rayDir = inverseView * glm::vec3(ray);

		return { rayPos, rayDir };
	}
}
