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

			// The panel size is the *output* size; the renderer decides the actual render
			// resolution from it (native, a fraction, or an absolute target). Everything that
			// defines what is rendered - framebuffer, camera aspect, scene viewport - has to follow
			// the render size, not the panel, or a fixed-resolution render comes out distorted.
			if (m_Renderer)
				m_Renderer->SetViewportSize((uint32_t)m_Size.x, (uint32_t)m_Size.y);

			const glm::uvec2 renderSize = GetRenderSize();
			const uint32_t width = renderSize.x;
			const uint32_t height = renderSize.y;

			if (m_Framebuffer->GetWidth() != width || m_Framebuffer->GetHeight() != height)
			{
				m_Framebuffer->Resize(width, height);
				m_Camera.SetViewportBounds(0, 0, width, height);
			}

			scene->SetTargetFramebuffer(m_Framebuffer);
			scene->OnViewportResize(width, height);
		}

		// Resolution the scene is actually rendered at. Matches the panel unless the renderer is
		// scaling (or pinned to an absolute target).
		glm::uvec2 GetRenderSize() const
		{
			if (m_Renderer && m_Renderer->GetViewportWidth() > 0 && m_Renderer->GetViewportHeight() > 0)
				return { m_Renderer->GetViewportWidth(), m_Renderer->GetViewportHeight() };

			return { (uint32_t)glm::max(1.0f, m_Size.x), (uint32_t)glm::max(1.0f, m_Size.y) };
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

			UpdateImageBounds();

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
		// Screen-space rect of the whole panel. Use for overlays anchored to the panel (toolbar,
		// stats HUD, corner widgets).
		const glm::vec2* GetBounds() const { return m_Bounds; }
		// Screen-space rect of the rendered image inside the panel. Equal to the panel rect unless
		// the render aspect differs, in which case the image is centred and letterboxed. Anything
		// mapping screen position to scene position - mouse picking, ImGuizmo - must use this, or
		// it will be offset by the bars.
		const glm::vec2* GetImageBounds() const { return m_ImageBounds; }
		glm::vec2 GetImageSize() const { return m_ImageBounds[1] - m_ImageBounds[0]; }
		bool IsFocused() const { return m_Focused; }
		bool IsHovered() const { return m_Hovered; }

		DisplayMode GetDisplayMode() const { return m_DisplayMode; }
		void SetDisplayMode(DisplayMode displayMode) { m_DisplayMode = displayMode; }
		bool IsSelectedWireframeMode() const { return m_DisplayMode == DisplayMode::SelectedWireframe; }

	private:
		// Fit the render aspect inside the panel and centre it, so a render whose aspect differs
		// from the panel is letterboxed instead of stretched. Aspects that match produce the full
		// panel rect and no bars, which is the usual case.
		void UpdateImageBounds()
		{
			const glm::uvec2 renderSize = GetRenderSize();
			if (m_Size.x <= 1.0f || m_Size.y <= 1.0f || renderSize.x == 0 || renderSize.y == 0)
			{
				m_ImageBounds[0] = m_Bounds[0];
				m_ImageBounds[1] = m_Bounds[1];
				return;
			}

			const float renderAspect = (float)renderSize.x / (float)renderSize.y;
			const float panelAspect = m_Size.x / m_Size.y;

			glm::vec2 imageSize = m_Size;
			if (panelAspect > renderAspect)
				imageSize.x = m_Size.y * renderAspect; // panel is wider - bars left/right
			else if (panelAspect < renderAspect)
				imageSize.y = m_Size.x / renderAspect; // panel is taller - bars top/bottom

			const glm::vec2 offset = (m_Size - imageSize) * 0.5f;
			m_ImageBounds[0] = m_Bounds[0] + offset;
			m_ImageBounds[1] = m_ImageBounds[0] + imageSize;
		}

		std::string m_Name;
		Ref<Framebuffer> m_Framebuffer;
		Ref<SceneRenderer> m_Renderer;
		EditorCamera m_Camera;
		glm::vec2 m_Size = { 0.0f, 0.0f };
		glm::vec2 m_Bounds[2] = {};
		glm::vec2 m_ImageBounds[2] = {};
		bool m_Focused = false;
		bool m_Hovered = false;
		DisplayMode m_DisplayMode = DisplayMode::Lit;
	};
}
