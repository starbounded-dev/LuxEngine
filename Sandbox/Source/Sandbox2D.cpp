#include "Sandbox2D.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Lux/Asset/TextureImporter.h"
#include "Lux/Core/Application.h"
#include "Lux/Core/Input.h"
#include "Lux/Core/Window.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Renderer/Camera.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/UI/Font.h"

#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include "nvrhi/utils.h"

#include <format>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Sandbox2D::Sandbox2D()
	: Layer("Sandbox2D")
	, m_EditorCamera(60.0f, 1600.0f, 900.0f, 0.1f, 1000.0f)
	, m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
{
}

// ---------------------------------------------------------------------------
// OnAttach
// ---------------------------------------------------------------------------

void Sandbox2D::OnAttach()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnAttach");

	m_CommandBuffer = Lux::RenderCommandBuffer::Create(0, "Sandbox2D");
	m_CheckerboardTexture = Lux::TextureImporter::LoadTexture2D("assets/textures/Checkerboard.png");

	// SetLineWidth is a static method.
	m_Renderer2D = Lux::Ref<Lux::Renderer2D>::Create();
	m_Renderer2D->SetLineWidth(2.0f);
}

// ---------------------------------------------------------------------------
// OnDetach
// ---------------------------------------------------------------------------

void Sandbox2D::OnDetach()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnDetach");
}

// ---------------------------------------------------------------------------
// OnUpdate
//
// The fix for magenta: Lux::Renderer2D is STATIC. BeginScene REQUIRES
// a RenderCommandBuffer and a Framebuffer/SwapChain as the first two args.
// The old overloads (BeginScene(Camera, mat4) etc.) are deprecated no-ops
// that only print a warning - they never draw anything. That was the bug.
//
// Frame structure:
//   1. Throttled FPS + perf timers
//   2. Viewport / ortho projection update; resize detection
//   3. Camera update
//   4. m_CommandBuffer->Begin()
//   5. Clear back-buffer via nvrhi
//   6. Scene geometry: BeginScene(cmd, swapchain, EditorCamera) + draws
//   7. Overlay: BeginScene(cmd, swapchain, ortho Camera) + text draws
//   8. m_CommandBuffer->End() + Submit()
//   9. Post-update deferred queue flush
// ---------------------------------------------------------------------------

void Sandbox2D::OnUpdate(Lux::Timestep ts)
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnUpdate");

	// ------------------------------------------------------------------
	// 1. Throttled stat updates (mirrors Hazel RuntimeLayer pattern)
	// ------------------------------------------------------------------

	m_UpdateFPSTimer -= ts;
	if (m_UpdateFPSTimer <= 0.0f)
	{
		UpdateFPSStat();
		m_UpdateFPSTimer = 1.0f;
	}

	m_UpdatePerfTimer -= ts;
	if (m_UpdatePerfTimer <= 0.0f)
	{
		UpdatePerformanceTimers();
		m_UpdatePerfTimer = 0.2f;
	}

	// ------------------------------------------------------------------
	// 2. Viewport + ortho projection
	// ------------------------------------------------------------------

	auto [width, height] = Lux::Application::Get().GetWindow().GetSize();

	// Screen-space ortho projection used for debug overlay text.
	// Origin at bottom-left, matching Hazel's m_Renderer2DProj.
	m_Renderer2DProj = glm::ortho(0.0f, (float)width, 0.0f, (float)height);
	m_EditorCamera.SetViewportBounds(0, 0, width, height);

	if (m_Width != width || m_Height != height)
		OnSwapChainResize(width, height);

	// ------------------------------------------------------------------
	// 3. Camera
	// ------------------------------------------------------------------

	m_EditorCamera.OnUpdate(ts);

	// ------------------------------------------------------------------
	// 4-8. Render
	// ------------------------------------------------------------------

	auto& swapchain = Lux::Application::Get().GetWindow().GetSwapChain();

	m_Renderer2D->ResetStats();

	m_CommandBuffer->Begin();

	// Clear the back-buffer using the swapchain's current framebuffer directly.
	// No SwapChainFramebuffer wrapper needed - VulkanSwapChain exposes it.
	Lux::Renderer::Submit([cmd = m_CommandBuffer, &swapchain]()
		{
			nvrhi::utils::ClearColorAttachment(cmd->GetActive(), swapchain.GetCurrentFramebuffer(), 0,
				nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f));
		});

	// 6. Scene geometry draw.
	// BeginScene(cmd, swapchain*, EditorCamera) is the correct non-deprecated
	// overload. Internally it calls SwapChainFramebuffer::Create and routes
	// all draw calls into the swap-chain back-buffer.
	{
		static float rotation = 0.0f;
		rotation += ts * 50.0f;

		LUX_PROFILE_SCOPE("Renderer Draw");

		Lux::Ref<Lux::Texture2D> bgTexture =
			m_CheckerboardTexture ? m_CheckerboardTexture : Lux::Renderer::GetWhiteTexture();

		m_Renderer2D->BeginScene(m_EditorCamera.GetViewProjection(), m_EditorCamera.GetViewMatrix());
		//m_Renderer2D->SetTargetFramebuffer(m_Framebuffer);
		m_Renderer2D->DrawQuad({ 0.0f, 0.0f, -5.0f }, { 20.0f, 20.0f }, bgTexture, 10.0f);
		m_Renderer2D->DrawRotatedQuad({ 1.0f, 0.0f, -5.0f }, { 0.8f,  0.8f }, rotation, { 0.8f, 0.2f, 0.3f, 1.0f });
		m_Renderer2D->EndScene();
	}

	// 7. 2D overlay (debug/FPS text in screen-space).
	OnRender2D();

	m_CommandBuffer->End();
	m_CommandBuffer->Submit();

	// ------------------------------------------------------------------
	// 9. Post-update deferred queue (VSync toggle, etc.)
	// ------------------------------------------------------------------

	auto queue = m_PostSceneUpdateQueue;
	m_PostSceneUpdateQueue.clear();
	for (auto& fn : queue)
		fn();
}

// ---------------------------------------------------------------------------
// OnRender2D
//
// Issues debug/FPS text draws using a second BeginScene/EndScene block with
// a screen-space ortho Camera wrapping m_Renderer2DProj. The same swapchain
// is targeted so overlays composite on top of the geometry above.
// ---------------------------------------------------------------------------

void Sandbox2D::OnRender2D()
{
	if (!m_ShowDebugDisplay)
		return;

	// Wrap the ortho matrix in a Lux::Camera so we can use the
	// BeginScene(cmd, swapchain*, Camera, transform) overload.
	// Identity is passed as the view transform (screen-space).
	Lux::Camera screenCam(m_Renderer2DProj, m_Renderer2DProj);

	m_Renderer2D->BeginScene(m_Renderer2DProj, glm::mat4(1.0f));

	DrawFPSStats();
	DrawDebugStats();

	m_Renderer2D->EndScene();
}

// ---------------------------------------------------------------------------
// OnSwapChainResize
// ---------------------------------------------------------------------------

void Sandbox2D::OnSwapChainResize(uint32_t width, uint32_t height)
{
	m_Width = width;
	m_Height = height;
	// SwapChainFramebuffer::Create is called fresh each frame, so no
	// explicit Renderer2D notification is needed on resize.
}

// ---------------------------------------------------------------------------
// DrawFPSStats - anchored to bottom-right corner
// ---------------------------------------------------------------------------

void Sandbox2D::DrawFPSStats()
{
	const float fontSize = 20.0f;
	glm::vec2 pos = { (float)m_Width - 250.0f, 40.0f };

	DrawString(std::format("{} fps", m_FramesPerSecond), pos, { 0.0f, 1.0f, 0.0f, 1.0f }, fontSize);
	pos.y += fontSize;
	DrawString(std::format("{:.2f} ms frame", m_FrameTime), pos, { 0.0f, 1.0f, 0.0f, 1.0f }, fontSize);
	pos.y += fontSize;
	DrawString(std::format("{:.2f} ms CPU", m_CPUTime), pos, { 0.0f, 1.0f, 0.0f, 1.0f }, fontSize);
	pos.y += fontSize;
	DrawString(std::format("{:.2f} ms GPU", m_GPUTime), pos, { 0.0f, 1.0f, 0.0f, 1.0f }, fontSize);
}

// ---------------------------------------------------------------------------
// DrawDebugStats - anchored to top-left corner
// ---------------------------------------------------------------------------

void Sandbox2D::DrawDebugStats()
{
	const float fontSize = 25.0f;
	glm::vec2 pos = { 20.0f, (float)m_Height - 50.0f };

	DrawString(std::format("{:.2f} ms frame", m_FrameTime), pos, glm::vec4(1.0f), fontSize);
	pos.y -= fontSize;
	DrawString(std::format("{:.2f} ms CPU", m_CPUTime), pos, glm::vec4(1.0f), fontSize);
	pos.y -= fontSize;
	DrawString(std::format("{:.2f} ms GPU", m_GPUTime), pos, glm::vec4(1.0f), fontSize);
	pos.y -= fontSize;
	DrawString(std::format("{} fps", m_FramesPerSecond), pos, glm::vec4(1.0f), fontSize);
}

// ---------------------------------------------------------------------------
// DrawString
//
// Renders text at a screen-space position using Renderer2D::DrawString with
// Lux::Renderer2D::TextParams. Must be called between BeginScene/EndScene
// with an ortho camera (see OnRender2D).
//
// Uses a one-pixel drop shadow at Z -0.20, then main text at Z -0.21,
// matching Hazel's RuntimeLayer::DrawString shadow technique.
// ---------------------------------------------------------------------------

void Sandbox2D::DrawString(const std::string& string,
	const glm::vec2& position,
	const glm::vec4& color,
	float size,
	bool shadow)
{
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(size));

	if (shadow)
	{
		const float offset = 1.0f;
		glm::mat4 shadowTransform = glm::translate(glm::mat4(1.0f),
			{ position.x + offset, position.y - offset, -0.20f }) * scale;

		m_Renderer2D->DrawString(string, Lux::Font::GetDefaultMonoSpacedFont(),
			shadowTransform, 1000.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	}

	glm::mat4 transform = glm::translate(glm::mat4(1.0f),
		{ position.x, position.y, -0.21f }) * scale;

	m_Renderer2D->DrawString(string, Lux::Font::GetDefaultMonoSpacedFont(),
		transform, 1000.0f, color);
}

// ---------------------------------------------------------------------------
// UpdateFPSStat - at most once per second
// ---------------------------------------------------------------------------

void Sandbox2D::UpdateFPSStat()
{
	m_FramesPerSecond = static_cast<uint32_t>(1.0f / (float)Lux::Application::Get().GetFrametime());
}

// ---------------------------------------------------------------------------
// UpdatePerformanceTimers - at 5 Hz
// ---------------------------------------------------------------------------

void Sandbox2D::UpdatePerformanceTimers()
{
	auto& app = Lux::Application::Get();
	m_FrameTime = (float)app.GetFrametime().GetMilliseconds();
	auto perf = app.GetPerformanceTimers();
	m_GPUTime = perf.RenderThreadGPUWaitTime;
	m_CPUTime = m_FrameTime - m_GPUTime;
}

// ---------------------------------------------------------------------------
// OnImGuiRender
// ---------------------------------------------------------------------------

void Sandbox2D::OnImGuiRender()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnImGuiRender");

	// Note: Switch this to true to enable dockspace
	static bool dockspaceOpen = true;
	static bool opt_fullscreen_persistant = true;
	bool opt_fullscreen = opt_fullscreen_persistant;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each others.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}

	// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive, 
	// all active windows docked into it will lose their parent and become undocked.
	// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise 
	// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
	ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// DockSpace
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	float minWinSizeX = style.WindowMinSize.x;
	style.WindowMinSize.x = 370.0f;
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}

	style.WindowMinSize.x = minWinSizeX;

	ImGui::Begin("Renderer Info");

	// Renderer2D::GetStats() - not GetDrawStats()
	auto stats = m_Renderer2D->GetDrawStats();
	ImGui::Text("Renderer2D Stats:");
	ImGui::Text("Draw Calls : %d", stats.DrawCalls);
	ImGui::Text("Quads      : %d", stats.QuadCount);
	ImGui::Text("Vertices   : %d", stats.GetTotalVertexCount());
	ImGui::Text("Indices    : %d", stats.GetTotalIndexCount());

	ImGui::Separator();

	ImGui::Text("Frame : %.3f ms (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
	ImGui::Text("CPU   : %.2f ms", m_CPUTime);
	ImGui::Text("GPU   : %.2f ms", m_GPUTime);

	ImGui::Separator();

	if (ImGui::Checkbox("VSync", &m_VSync))
	{
		// Deferred so the window call happens outside the render loop.
		m_PostSceneUpdateQueue.push_back([this]()
			{
				Lux::Application::Get().GetWindow().SetVSync(m_VSync);
			});
	}

	ImGui::Checkbox("Debug overlay (Ctrl+F3)", &m_ShowDebugDisplay);

	ImGui::End();
	
	ImGui::Begin("Viewport");

	auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
	auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
	auto viewportOffset = ImGui::GetWindowPos();
	m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
	m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

	m_ViewportFocused = ImGui::IsWindowFocused();
	m_ViewportHovered = ImGui::IsWindowHovered();
	Lux::Application::Get().GetImGuiLayer()->AllowInputEvents(!m_ViewportFocused && !m_ViewportHovered);

	ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
	m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

	ImTextureID texID = (m_Framebuffer->GetImage(0));
	ImGui::Image(texID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0,1 }, ImVec2{ 1,0 });

	m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

	ImGui::End();
}

// ---------------------------------------------------------------------------
// OnEvent
// ---------------------------------------------------------------------------

void Sandbox2D::OnEvent(Lux::Event& e)
{
	m_EditorCamera.OnEvent(e);

	Lux::EventDispatcher dispatcher(e);
	dispatcher.Dispatch<Lux::KeyPressedEvent>([this](Lux::KeyPressedEvent& ke) -> bool
		{
			if (ke.GetRepeatCount() == 0 && Lux::Input::IsKeyDown(Lux::KeyCode::LeftControl))
			{
				if (ke.GetKeyCode() == Lux::KeyCode::F3)
				{
					m_ShowDebugDisplay = !m_ShowDebugDisplay;
					return true;
				}
			}
			return false;
		});
}
