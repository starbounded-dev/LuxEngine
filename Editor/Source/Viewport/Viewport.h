#pragma once

#include "Lux/Editor/EditorCamera.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/Image.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Scene/Scene.h"

#include <imgui/imgui.h>

#include <string>
#include <utility>

namespace Lux
{
	class Viewport : public RefCounted
	{
	public:
		enum class DisplayMode
		{
			Lit = 0,
			SelectedWireframe = 1
		};

	public:
		Viewport(std::string name = "Viewport")
			: m_Name(std::move(name)), m_Camera(60.0f, 1600.0f, 900.0f, 0.1f, 10000.0f)
		{
		}

		void Init(Ref<Scene> scene, const FramebufferSpecification& framebufferSpec, const SceneRendererSpecification& rendererSpec)
		{
			m_Framebuffer = Framebuffer::Create(framebufferSpec);
			m_Renderer = Ref<SceneRenderer>::Create(scene, rendererSpec);
			m_Camera = EditorCamera(60.0f, (float)framebufferSpec.Width, (float)framebufferSpec.Height, 0.1f, 10000.0f);
			m_Camera.SetActive(true);

			if (scene)
				scene->SetTargetFramebuffer(m_Framebuffer);
		}

		void Shutdown()
		{
			m_Renderer.reset();
			m_Framebuffer.reset();
		}

		void SetScene(Ref<Scene> scene)
		{
			if (scene && m_Framebuffer)
				scene->SetTargetFramebuffer(m_Framebuffer);

			if (m_Renderer)
				m_Renderer->SetScene(scene);
		}

		void SyncSceneViewport(Ref<Scene> scene)
		{
			if (!scene || !m_Framebuffer)
				return;

			if (m_Size.x <= 1.0f || m_Size.y <= 1.0f)
				return;

			const uint32_t width = (uint32_t)m_Size.x;
			const uint32_t height = (uint32_t)m_Size.y;

			if (m_Framebuffer->GetWidth() != width || m_Framebuffer->GetHeight() != height)
			{
				m_Framebuffer->Resize(width, height);
				m_Camera.SetViewportBounds(0, 0, width, height);
			}

			scene->SetTargetFramebuffer(m_Framebuffer);
			scene->OnViewportResize(width, height);

			if (m_Renderer)
				m_Renderer->SetViewportSize(width, height);
		}

		bool BeginImGui()
		{
			const bool visible = ImGui::Begin(m_Name.c_str());
			if (!visible)
			{
				m_Focused = false;
				m_Hovered = false;
				return false;
			}

			const ImVec2 viewportMinRegion = ImGui::GetWindowContentRegionMin();
			const ImVec2 viewportMaxRegion = ImGui::GetWindowContentRegionMax();
			const ImVec2 viewportOffset = ImGui::GetWindowPos();

			m_Bounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
			m_Bounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

			m_Focused = ImGui::IsWindowFocused();
			m_Hovered = ImGui::IsWindowHovered();

			// Avoid propagating zero-sized dock/drag transitions into the renderer.
			const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
			if (viewportPanelSize.x > 1.0f && viewportPanelSize.y > 1.0f)
				m_Size = { viewportPanelSize.x, viewportPanelSize.y };

			return m_Size.x > 1.0f && m_Size.y > 1.0f;
		}

		void EndImGui()
		{
			ImGui::End();
		}

		Ref<Image2D> GetDisplayImage()
		{
			if (m_Renderer && m_Renderer->GetFinalPassImage())
				return m_Renderer->GetFinalPassImage();

			return m_Framebuffer ? m_Framebuffer->GetImage(0) : nullptr;
		}

		Ref<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }
		Ref<SceneRenderer> GetSceneRenderer() const { return m_Renderer; }
		EditorCamera& GetCamera() { return m_Camera; }
		const EditorCamera& GetCamera() const { return m_Camera; }

		const glm::vec2& GetSize() const { return m_Size; }
		const glm::vec2* GetBounds() const { return m_Bounds; }
		bool IsFocused() const { return m_Focused; }
		bool IsHovered() const { return m_Hovered; }

		DisplayMode GetDisplayMode() const { return m_DisplayMode; }
		void SetDisplayMode(DisplayMode displayMode) { m_DisplayMode = displayMode; }
		bool IsSelectedWireframeMode() const { return m_DisplayMode == DisplayMode::SelectedWireframe; }

	private:
		std::string m_Name;
		Ref<Framebuffer> m_Framebuffer;
		Ref<SceneRenderer> m_Renderer;
		EditorCamera m_Camera;
		glm::vec2 m_Size = { 0.0f, 0.0f };
		glm::vec2 m_Bounds[2] = {};
		bool m_Focused = false;
		bool m_Hovered = false;
		DisplayMode m_DisplayMode = DisplayMode::Lit;
	};
}
