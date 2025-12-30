﻿#pragma once

#include "StarEngine/Core/Ref.h"
#include "StarEngine/Editor/EditorCamera.h"
#include "StarEngine/Scene/Entity.h"

#include <glm/glm.hpp>
#include <string>
#include <array>


namespace StarEngine
{
	class SceneRenderer;
	class Renderer2D;
	class Scene;
	class EditorLayer;
	class KeyPressedEvent;
	class MouseButtonPressedEvent;

	class Viewport : public RefCounted
	{
	public:
		Viewport(const std::string& viewportName, EditorLayer* editorLayer);

		const std::string& GetName() const;

		bool IsViewportVisible() const;
		bool IsMainViewport() const;

		Ref<SceneRenderer> GetRenderer() const;
		Ref<Renderer2D> GetRenderer2D() const;

		EditorCamera& GetViewportCamera();
		std::array<glm::vec2, 2> GetViewportBounds() const;

		void Init(const Ref<Scene>& scene);

		void SetIsMainViewport(bool isMain);
		void SetIsVisible(bool visible);

		void OnUpdate(Timestep ts);
		void OnRender();
		void OnRender2D();
		void OnImGuiRender();
		void OnEvent(Event& e);

		void OnProjectClosed();
		void OnScenePlay();
		void OnNewScene(const Ref<Scene>& newScene);
		void OnOpenScene(const Ref<Scene>& scene);

		void ResetCamera();

		bool* GetIsVisibleMemory();

	private:
		void UI_DrawGizmos();
		void UI_GizmosToolbar();
		void UI_CentralToolbar();
		void UI_ViewportSettings();
		void UI_HandleAssetDrop();

		Entity GetSelectedCameraEntity();

		void DrawCameraFrustum(Entity& entity);
		void DrawSelectedCameraPreview();

		bool OnKeyPressedEvent(KeyPressedEvent& e);
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

		std::pair<float, float> GetMouseViewportSpace(bool primaryViewport) const;
		std::pair<glm::vec3, glm::vec3> CastRay(float mx, float my) const;

		WeakRef<EditorLayer> m_EditorLayer;

		inline static float s_SelectedCameraSizePercent = 0.2f;

		std::string m_ViewportName;
		Ref<SceneRenderer> m_ViewportRenderer;
		Ref<SceneRenderer> m_SelectedCameraRenderer;
		Ref<Renderer2D> m_ViewportRenderer2D;
		EditorCamera m_ViewportCamera;
		std::array<glm::vec2, 2> m_ViewportBounds = {};
		std::array<glm::vec2, 2> m_SelectedCameraBounds = {};
		glm::vec2 m_ViewportSize = glm::vec2(0.f);

		// Main means the last active viewport, so if you click on any viewport and it is focus this viewport will be main. And from this one You'll be able to start playing
		bool m_IsMainViewport = false;
		bool m_IsVisible = false;
		bool m_IsMouseOver = false;
		bool m_IsFocused = false;
		bool m_ShowIcons = true;
		bool m_ShowGizmos = true;
		bool m_ShowGizmosInPlayMode = false;
		bool m_ShowBoundingBoxSelectedMeshOnly = true;
		bool m_ShowBoundingBoxSubmeshes = false;
		bool m_DrawOnTopBoundingBoxes = true;
		bool m_ShowBoundingBoxes = false;

		float m_LineWidth = 2.0f;

		enum class SelectionMode { Entity = 0, SubMesh = 1 };
		SelectionMode m_SelectionMode = SelectionMode::Entity;

		enum class TransformationTarget { MedianPoint, IndividualOrigins };
		TransformationTarget m_MultiTransformTarget = TransformationTarget::MedianPoint;

		Entity m_SelectedCameraEntity;
	};
}

