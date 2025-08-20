#pragma once

#include "Camera.h"
#include "StarEngine/Renderer/RenderCommand.h"

#include "StarEngine/Renderer/Camera.h"
#include "StarEngine/Renderer/Shader.h"

namespace StarEngine {

	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();

		static void OnWindowResize(uint32_t width, uint32_t height);

		static void BeginScene(Camera& camera);
		static void EndScene();

		static void Submit(Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	private:
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
		};
		static Scope<SceneData> s_SceneData;
	};
}
