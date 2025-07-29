#include "Sandbox2D.h"

#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui/imgui_internal.h"

#include "StarEngine/Asset/TextureImporter.h"


Sandbox2D::Sandbox2D()
	:Layer("Sandbox2D"), m_CameraController(1280.0f / 720.0f, true), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
{

}

void Sandbox2D::OnAttach()
{
	SE_PROFILE_FUNCTION("Sandbox2D::OnAttach");

	//m_CheckerboardTexture = StarEngine::TextureImporter::LoadTexture2D("assets/textures/Checkerboard.png");
}

void Sandbox2D::OnDetach()
{
	SE_PROFILE_FUNCTION("Sandbox2D::OnDetach");
}

void Sandbox2D::OnUpdate(StarEngine::Timestep ts)
{
	SE_PROFILE_FUNCTION("Sandbox2D::OnUpdate");

	// Update
	m_CameraController.OnUpdate(ts);
	// Render
	StarEngine::Renderer2D::ResetStats();
	{
		SE_PROFILE_SCOPE("Renderer Prep");
		StarEngine::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		StarEngine::RenderCommand::Clear();
	}
	
	{
		static float rotation = 0.0f;
		rotation += ts * 50.0f;

		SE_PROFILE_SCOPE("Renderer Draw");

		StarEngine::Renderer2D::BeginScene(m_CameraController.GetCamera());
		//StarEngine::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 20.0f, 20.0f }, m_CheckerboardTexture, 10.0f);
		//StarEngine::Renderer2D::DrawRotatedQuad({ 1.0f, 0.0f }, { 0.8f, 0.8f }, rotation, { 0.8f, 0.2f, 0.3f, 1.0f });
		StarEngine::Renderer2D::EndScene();
	}
}

void Sandbox2D::OnImGuiRender()
{
	SE_PROFILE_FUNCTION("Sandbox2D::OnImGuiRender");

	ImGuiContext& g = *GImGui;
	ImGuiIO& io = g.IO;

	//Camera Info
	ImGui::Begin("Camera Info");

	auto stats = StarEngine::Renderer2D::GetStats();
	ImGui::Text("Renderer2D Stats:");
	ImGui::Text("Draw Calls: %d", stats.DrawCalls);
	ImGui::Text("Quads: %d", stats.QuadCount);
	ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
	ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

	ImGui::Separator();

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	if (ImGui::Checkbox("VSync", &m_VSync)) {
		// Toggle VSync when the checkbox is clicked
		StarEngine::Application::Get().GetWindow().SetVSync(m_VSync);
	}

	ImGui::End();
}

void Sandbox2D::OnEvent(StarEngine::Event& e)
{
	m_CameraController.OnEvent(e);
}
