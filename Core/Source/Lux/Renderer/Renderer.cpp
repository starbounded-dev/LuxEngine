#include "sepch.h"

#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Renderer/Renderer2D.h"

#include "StarEngine/Audio/AudioEngine.h"

namespace StarEngine {

	Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();

	void Renderer::Init()
	{
		SE_PROFILE_FUNCTION("Renderer::Init");

		AudioEngine::Init();

		RenderCommand::Init();
		Renderer2D::Init();
	}

	void Renderer::Shutdown()
	{
		SE_PROFILE_FUNCTION("Renderer::Shutdown");
		Renderer2D::Shutdown();

		AudioEngine::Shutdown();
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}

	void Renderer::BeginScene(OrthographicCamera& camera)
	{
		SE_PROFILE_FUNCTION("Renderer::BeginScene");
		s_SceneData->ViewProjectionMatrix = camera.GetViewProjectionMatrix();
	}

	void Renderer::EndScene()
	{
		SE_PROFILE_FUNCTION("Renderer::EndScene");
		// Flush any remaining 2D rendering commands
		Renderer2D::Flush();
		// Reset the scene data
		s_SceneData->ViewProjectionMatrix = glm::mat4(1.0f);
	}

	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform)
	{
		SE_PROFILE_FUNCTION("Renderer::Submit");
		shader->Bind();

		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}
}
