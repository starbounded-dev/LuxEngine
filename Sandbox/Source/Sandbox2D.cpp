#include "Sandbox2D.h"

#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui/imgui_internal.h"

#include "Lux/Asset/TextureImporter.h"


Sandbox2D::Sandbox2D()
	:Layer("Sandbox2D"), m_CameraController(1280.0f / 720.0f, true), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
{

}

void Sandbox2D::OnAttach()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnAttach");

	//m_CheckerboardTexture = Lux::TextureImporter::LoadTexture2D("assets/textures/Checkerboard.png");
}

void Sandbox2D::OnDetach()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnDetach");
}

void Sandbox2D::OnUpdate(Lux::Timestep ts)
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnUpdate");

	// Update
	m_CameraController.OnUpdate(ts);
	// Render
	Lux::Renderer2D::ResetStats();
	{
		LUX_PROFILE_SCOPE("Renderer Prep");
		Lux::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		Lux::RenderCommand::Clear();
	}
	
	{
		static float rotation = 0.0f;
		rotation += ts * 50.0f;

		LUX_PROFILE_SCOPE("Renderer Draw");

		Lux::Renderer2D::BeginScene(m_CameraController.GetCamera());
		//Lux::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 20.0f, 20.0f }, m_CheckerboardTexture, 10.0f);
		//Lux::Renderer2D::DrawRotatedQuad({ 1.0f, 0.0f }, { 0.8f, 0.8f }, rotation, { 0.8f, 0.2f, 0.3f, 1.0f });
		Lux::Renderer2D::EndScene();
	}
}

void Sandbox2D::OnImGuiRender()
{
	LUX_PROFILE_FUNCTION("Sandbox2D::OnImGuiRender");

	ImGuiContext& g = *GImGui;
	ImGuiIO& io = g.IO;

	//Camera Info
	ImGui::Begin("Camera Info");

	auto stats = Lux::Renderer2D::GetStats();
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
	m_CameraController.OnEvent(e);
}
