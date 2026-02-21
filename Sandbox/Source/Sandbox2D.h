#pragma once

#include "Lux.h"

#include <functional>
#include <vector>

class Sandbox2D : public Lux::Layer
{
public:
	Sandbox2D();
	virtual ~Sandbox2D() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	void OnUpdate(Lux::Timestep ts) override;
	virtual void OnImGuiRender() override;
	void OnEvent(Lux::Event& e) override;

private:
	// Draws 2D overlays (FPS, debug stats) as a second BeginScene/EndScene pass.
	void OnRender2D();

	void OnViewportResize(uint32_t width, uint32_t height);

	// Draws FPS + frame timing lines anchored to the bottom-right corner.
	void DrawFPSStats();

	// Draws a broader timing breakdown anchored to the top-left corner.
	void DrawDebugStats();

	// Renders text at a 2D screen-space position with an optional drop shadow.
	// Must be called between BeginScene/EndScene with an ortho projection.
	void DrawString(const std::string& string,
		const glm::vec2& position,
		const glm::vec4& color = glm::vec4(1.0f),
		float size = 20.0f,
		bool shadow = true);

	// Throttled stat helpers - called from OnUpdate timers.
	void UpdateFPSStat();
	void UpdatePerformanceTimers();

private:
	Lux::EditorCamera m_EditorCamera;

	// Ortho projection rebuilt every frame to match viewport size.
	// Used for the debug/FPS overlay BeginScene call.
	glm::mat4 m_Renderer2DProj{ 1.0f };

	// Offscreen framebuffer the scene is rendered into.
	// Displayed as an ImGui::Image in the Viewport panel.
	// Renderer2D targets this via SetTargetFramebuffer().
	Lux::Ref<Lux::Framebuffer> m_Framebuffer;

	Lux::Ref<Lux::Texture2D> m_CheckerboardTexture;

	// Renderer2D is self-contained: it owns its own RenderCommandBuffer and
	// internal RenderPasses (quad/line/text). All we do is call
	// SetTargetFramebuffer() once, then BeginScene/EndScene each frame.
	Lux::Ref<Lux::Renderer2D> m_Renderer2D;

	// Viewport dimensions - tracked so we detect resize each frame.
	glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
	uint32_t m_Width = 0;
	uint32_t m_Height = 0;

	// Throttled FPS / perf timers.
	uint32_t m_FramesPerSecond = 0;
	float    m_UpdateFPSTimer = 0.0f; // resets to 1.0 s
	float    m_UpdatePerfTimer = 0.0f; // resets to 0.2 s

	float m_FrameTime = 0.0f;
	float m_CPUTime = 0.0f;
	float m_GPUTime = 0.0f;

	// Ctrl+F3 toggles the on-screen debug overlay.
	bool m_ShowDebugDisplay = false;

	// Lambdas pushed here are flushed at the end of OnUpdate (post-render).
	std::vector<std::function<void()>> m_PostSceneUpdateQueue;

	glm::vec2 m_ViewportBounds[2];

	bool m_VSync = true;
	bool m_ViewportFocused = false;
	bool m_ViewportHovered = false;

	glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
};
