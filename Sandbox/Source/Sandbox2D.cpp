#include "Sandbox2D.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Lux/Core/Application.h"
#include "Lux/Core/Input.h"
#include "Lux/Core/Window.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/RendererTypes.h"
#include "Lux/Renderer/UI/Font.h"

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

	Lux::TextureSpecification checkerboardSpec;
	checkerboardSpec.Format = Lux::ImageFormat::SRGBA;
	checkerboardSpec.GenerateMips = false;
	checkerboardSpec.DebugName = "assets/textures/Checkerboard.png";
	m_CheckerboardTexture = Lux::Texture2D::Create(checkerboardSpec, "assets/textures/Checkerboard.png");

	// ------------------------------------------------------------------
	// Renderer2D
	// Renderer2D is self-contained: it creates its own RenderCommandBuffer,
	// internal framebuffer, and RenderPasses (quad/line/text) inside Init().
	// ------------------------------------------------------------------
	m_Renderer2D = Lux::Ref<Lux::Renderer2D>::Create();
	m_Renderer2D->SetLineWidth(2.0f);

	// Camera must be explicitly activated or OnUpdate returns immediately.
	m_EditorCamera.SetActive(true);

	// ------------------------------------------------------------------
	// Offscreen Framebuffer
	// We create our own framebuffer and hand it to Renderer2D via
	// SetTargetFramebuffer(). This rebuilds all internal pipelines to
	// target our FB, so draws go into it instead of the default one.
	// The colour attachment (index 0) is then shown in the ImGui viewport.
	// ------------------------------------------------------------------
	Lux::FramebufferSpecification fbSpec;
	fbSpec.Attachments = { Lux::ImageFormat::RGBA32F, Lux::ImageFormat::Depth };
	fbSpec.ClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
	fbSpec.ClearColorOnLoad = true;
	fbSpec.ClearDepthOnLoad = true;
	fbSpec.DebugName = "Sandbox2D-Viewport";
	m_Framebuffer = Lux::Framebuffer::Create(fbSpec);

	// Point Renderer2D at our framebuffer. Internally this calls
	// Pipeline::Create for quad/line/text with TargetFramebuffer = m_Framebuffer.
	m_Renderer2D->SetTargetFramebuffer(m_Framebuffer);
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
// Frame structure:
//   1. Throttled FPS + perf timers
//   2. Viewport resize detection  →  resize framebuffer + camera
//   3. Camera update
//   4. Scene pass:   BeginScene(editorCam VP+V)  →  draws  →  EndScene
//   5. Overlay pass: BeginScene(orthoProj, I)    →  text   →  EndScene
//      (both passes submit their own command buffers internally)
//   6. Post-update deferred queue
// ---------------------------------------------------------------------------

void Sandbox2D::OnUpdate(Lux::Timestep ts)
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnUpdate");

	// ------------------------------------------------------------------
	// 1. Throttled stat updates
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
	// 2. Viewport resize detection
	// m_ViewportSize is written by OnImGuiRender the previous frame.
	// When it differs from our tracked size, resize the framebuffer and
	// update the camera aspect ratio.
	// ------------------------------------------------------------------

	if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
	{
		const uint32_t vpW = static_cast<uint32_t>(m_ViewportSize.x);
		const uint32_t vpH = static_cast<uint32_t>(m_ViewportSize.y);

		if (vpW != m_Width || vpH != m_Height)
			OnViewportResize(vpW, vpH);
	}

	// Screen-space ortho projection for the debug overlay.
	// Y=0 at top, Y=height at bottom - matches Vulkan clip-space where Y increases downward.
	// Without this flip the font atlas UV coords render the glyphs upside down.
	m_Renderer2DProj = glm::ortho(0.0f, m_ViewportSize.x, m_ViewportSize.y, 0.0f);

	// ------------------------------------------------------------------
	// 3. Camera
	// ------------------------------------------------------------------

	m_EditorCamera.OnUpdate(ts);

	// ------------------------------------------------------------------
	// 4. Scene geometry pass
	// BeginScene uploads the camera UBO and resets all vertex buffer ptrs.
	// EndScene flushes all batches: for each batch it calls
	//   Renderer::BeginRenderPass / RenderGeometry / EndRenderPass
	// and submits its own internal RenderCommandBuffer.
	// ------------------------------------------------------------------

	m_Renderer2D->ResetStats();

	{
		// Accumulate in degrees, convert to radians for glm::rotate.
		// 50 deg/s is a gentle, visible spin. The old ts * 50.0f was
		// 50 rad/s (~8 full rotations per second) which looked insane.
		static float rotation = 0.0f;
		rotation += ts * 50.0f; // degrees per second

		LUX_PROFILE_SCOPE("Renderer Draw");

		Lux::Ref<Lux::Texture2D> bgTexture =
			m_CheckerboardTexture ? m_CheckerboardTexture : Lux::Renderer::GetWhiteTexture();

		m_Renderer2D->BeginScene(m_EditorCamera.GetViewProjection(),
			m_EditorCamera.GetViewMatrix());

		// Background at z=-0.1 so it is always behind the rotating quad.
		// Pass uv1={10,10} so the texture coordinates actually span 0→10
		// across the quad, giving the repeating tile effect. The tilingFactor
		// argument alone doesn't scale UVs - the shader uses it as a multiplier
		// only when the vertex UVs cover the full 0→1 range first.
		m_Renderer2D->DrawQuad({ 0.0f, 0.0f, -0.1f }, { 20.0f, 20.0f }, bgTexture,
			1.0f, glm::vec4(1.0f), glm::vec2(0.0f), glm::vec2(10.0f));

		// Rotating quad at z=0, clearly in front of the background.
		m_Renderer2D->DrawRotatedQuad({ 1.0f, 0.0f, 0.0f }, { 0.8f, 0.8f },
			glm::radians(rotation),
			{ 0.8f, 0.2f, 0.3f, 1.0f });

		m_Renderer2D->EndScene();
	}

	// ------------------------------------------------------------------
	// 5. 2D overlay pass (debug/FPS text, screen-space, no depth test)
	// ------------------------------------------------------------------

	OnRender2D();

	// ------------------------------------------------------------------
	// 6. Post-update deferred queue (e.g. VSync toggle)
	// ------------------------------------------------------------------

	auto queue = m_PostSceneUpdateQueue;
	m_PostSceneUpdateQueue.clear();
	for (auto& fn : queue)
		fn();
}

// ---------------------------------------------------------------------------
// OnRender2D
//
// Second BeginScene/EndScene using a screen-space ortho projection so all
// text draws land in pixel coordinates on top of the scene, still inside
// m_Framebuffer (Renderer2D targets the same framebuffer for both passes).
// depthTest = false so text isn't occluded by scene geometry depth.
// ---------------------------------------------------------------------------

void Sandbox2D::OnRender2D()
{
	if (!m_ShowDebugDisplay)
		return;

	m_Renderer2D->BeginScene(m_Renderer2DProj,
		glm::mat4(1.0f), // identity view = screen-space
		false);          // depthTest off

	DrawFPSStats();
	DrawDebugStats();

	m_Renderer2D->EndScene();
}

// ---------------------------------------------------------------------------
// OnViewportResize
//
// Resizes m_Framebuffer to match the new ImGui viewport panel dimensions.
// The Framebuffer::Resize call recreates all attachment images at the new
// size via RT_Invalidate. The Renderer2D pipelines hold a Ref to the same
// Framebuffer object, so they automatically use the new images next frame.
// ---------------------------------------------------------------------------

void Sandbox2D::OnViewportResize(uint32_t width, uint32_t height)
{
	m_Width = width;
	m_Height = height;

	m_Framebuffer->Resize(width, height);

	// SetViewportBounds already handles the projection matrix update internally:
	// when (right-left) or (bottom-top) differs from the previous size it calls
	// SetPerspectiveProjectionMatrix, so this is the only call needed.
	m_EditorCamera.SetViewportBounds(0, 0, width, height);
}

// ---------------------------------------------------------------------------
// DrawFPSStats - anchored to bottom-right corner
// ---------------------------------------------------------------------------

void Sandbox2D::DrawFPSStats()
{
	const float fontSize = 18.0f;
	const float margin = 10.0f;

	glm::vec2 pos = {
		m_ViewportSize.x - 200.0f - margin,
		margin
	};

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
	glm::vec2 pos = { 20.0f, m_ViewportSize.y - 50.0f };

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
// UpdateFPSStat
// ---------------------------------------------------------------------------

void Sandbox2D::UpdateFPSStat()
{
	m_FramesPerSecond = static_cast<uint32_t>(1.0f / (float)Lux::Application::Get().GetFrametime());
}

// ---------------------------------------------------------------------------
// UpdatePerformanceTimers
// ---------------------------------------------------------------------------

void Sandbox2D::UpdatePerformanceTimers()
{
	auto& app = Lux::Application::Get();
	m_FrameTime = (float)app.GetFrametime().GetMilliseconds();

	// MainThreadWorkTime is the CPU time actually measured in Application::Run().
	// RenderThreadGPUWaitTime exists in the struct but is never populated by the
	// engine yet, so GPU time is shown as (FrameTime - CPU) as a best approximation.
	const auto& perf = app.GetPerformanceTimers();
	m_CPUTime = perf.MainThreadWorkTime;
	m_GPUTime = m_FrameTime - m_CPUTime;
}

// ---------------------------------------------------------------------------
// OnImGuiRender
// ---------------------------------------------------------------------------

void Sandbox2D::OnImGuiRender()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnImGuiRender");

	static bool dockspaceOpen = true;
	static bool opt_fullscreen_persistant = true;
	bool opt_fullscreen = opt_fullscreen_persistant;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}

	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
	ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

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

	// ------------------------------------------------------------------
	// Renderer info panel
	// ------------------------------------------------------------------

	ImGui::Begin("Renderer Info");

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
		m_PostSceneUpdateQueue.push_back([this]()
			{
				Lux::Application::Get().GetWindow().SetVSync(m_VSync);
			});
	}

	ImGui::Checkbox("Debug overlay (Ctrl+F3)", &m_ShowDebugDisplay);

	ImGui::End(); // Renderer Info

	// ------------------------------------------------------------------
	// Viewport panel
	//
	// Displays m_Framebuffer's colour attachment (index 0), which holds
	// the fully composited scene + overlay written by OnUpdate.
	//
	// We only RECORD the panel size here. Actual framebuffer resize happens
	// at the top of OnUpdate via OnViewportResize(), keeping all GPU work
	// on the render-thread path and away from the ImGui submission path.
	// ------------------------------------------------------------------

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Viewport");
	ImGui::PopStyleVar();

	auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
	auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
	auto viewportOffset = ImGui::GetWindowPos();
	m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x,
							 viewportMinRegion.y + viewportOffset.y };
	m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x,
							 viewportMaxRegion.y + viewportOffset.y };

	m_ViewportFocused = ImGui::IsWindowFocused();
	m_ViewportHovered = ImGui::IsWindowHovered();
	Lux::Application::Get().GetImGuiLayer()->AllowInputEvents(m_ViewportFocused || m_ViewportHovered);

	// Store panel size; OnUpdate will resize the framebuffer next frame if it changed.
	// Guard against zero/negative size which occurs while the panel is being dragged
	// or docked - passing 0 to Framebuffer::Resize frees images and produces an
	// invalid 0xFFFFFFFFFFFFFFFF handle that crashes the ImGui renderer.
	ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
	if (viewportPanelSize.x > 1.0f && viewportPanelSize.y > 1.0f)
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

	// CreateFrameTexture registers the NVRHI texture in the shared registry
	// for this frame and returns an encoded index handle.  All ImGuiRenderer
	// instances (main + per-viewport) share the same registry, so this handle
	// is valid when any of them decode it during draw-command processing.
	// UV flip (0,1)->(1,0) corrects Vulkan's top-left NDC origin.
	nvrhi::ITexture* nvrhiTex = m_Framebuffer->GetImage(0)->GetHandle().Get();
	ImTextureID texID = Lux::Application::Get().GetImGuiLayer()->GetImGuiRenderer()
	                                           ->CreateFrameTexture(nvrhiTex);
	ImGui::Image(texID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
		ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

	ImGui::End(); // Viewport

	ImGui::End(); // DockSpace Demo
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
