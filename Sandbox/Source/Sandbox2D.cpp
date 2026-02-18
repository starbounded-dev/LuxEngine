#include "Sandbox2D.h"

#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui/imgui_internal.h"

#include "Lux/Asset/TextureImporter.h"
#include "Lux/Core/Application.h"
#include "Lux/Core/Window.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/SwapChainFramebuffer.h"

#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include "nvrhi/utils.h"

Sandbox2D::Sandbox2D()
	:Layer("Sandbox2D"), m_EditorCamera(60.0f, 1600.0f, 900.0f, 0.1f, 1000.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
{

}

void Sandbox2D::OnAttach()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnAttach");

	m_CommandBuffer = Lux::RenderCommandBuffer::Create(0, "Sandbox2D");
	m_CheckerboardTexture = Lux::TextureImporter::LoadTexture2D("assets/textures/Checkerboard.png");

	m_Renderer2D = Lux::Ref<Lux::Renderer2D>::Create();
	m_Renderer2D->SetLineWidth(2.0f);
}

void Sandbox2D::OnDetach()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnDetach");
}

void Sandbox2D::OnUpdate(Lux::Timestep ts)
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnUpdate");

	// Update
	m_EditorCamera.OnUpdate(ts);

	// Render
		m_Renderer2D->ResetStats();

	auto [width, height] = Lux::Application::Get().GetWindow().GetSize();
	m_Renderer2DProj = glm::ortho(0.0f, (float)width, 0.0f, (float)height);
	m_EditorCamera.SetViewportBounds(0, 0, width, height);

	auto& swapchain = Lux::Application::Get().GetWindow().GetSwapChain();
	Lux::Ref<Lux::Framebuffer> framebuffer = Lux::SwapChainFramebuffer::Create(&swapchain);

	m_CommandBuffer->Begin();

	Lux::Renderer::Submit([cmd = m_CommandBuffer, fb = framebuffer]()
		{
			nvrhi::utils::ClearColorAttachment(cmd->GetActive(), fb->GetHandle(), 0,
				nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f));
		});

	{
		static float rotation = 0.0f;
		rotation += ts * 50.0f;

		LUX_PROFILE_SCOPE("Renderer Draw");

		Lux::Ref<Lux::Texture2D> bgTexture = m_CheckerboardTexture ? m_CheckerboardTexture : Lux::Renderer::GetWhiteTexture();
		// Put geometry well inside the frustum to avoid any near-plane clipping ambiguity
		m_Renderer2D->BeginScene(m_Renderer2DProj, glm::mat4(1.0f));
		m_Renderer2D->DrawQuad({ 0.0f, 0.0f, -5.0f }, { 20.0f, 20.0f }, bgTexture, 10.0f);
		m_Renderer2D->DrawRotatedQuad({ 1.0f, 0.0f, -5.0f }, { 0.8f, 0.8f }, rotation, { 0.8f, 0.2f, 0.3f, 1.0f });
		m_Renderer2D->EndScene();
	}

	m_CommandBuffer->End();
	m_CommandBuffer->Submit();
}

void Sandbox2D::OnImGuiRender()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnImGuiRender");

	ImGuiContext& g = *GImGui;
	ImGuiIO& io = g.IO;

	//Camera Info
	ImGui::Begin("Camera Info");

	auto stats = m_Renderer2D->GetDrawStats();
	ImGui::Text("Renderer2D Stats:");
	ImGui::Text("Draw Calls: %d", stats.DrawCalls);
	ImGui::Text("Quads: %d", stats.QuadCount);
	ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
	ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

	ImGui::Separator();

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	if (ImGui::Checkbox("VSync", &m_VSync)) {
		// Toggle VSync when the checkbox is clicked
		Lux::Application::Get().GetWindow().SetVSync(m_VSync);
	}

	ImGui::End();
}

void Sandbox2D::OnEvent(Lux::Event& e)
{
	m_EditorCamera.OnEvent(e);
}
