#pragma once

#include "StarEngine.h"
#include "StarEngine/Renderer/OrthographicCameraController.h"

class ExampleLayer : public StarEngine::Layer
{

public:
	ExampleLayer();
	virtual ~ExampleLayer() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	void OnUpdate(StarEngine::Timestep ts) override;
	virtual void OnImGuiRender() override;
	void OnEvent(StarEngine::Event& e) override;
private:
	StarEngine::ShaderLibrary m_ShaderLibrary;
	StarEngine::Ref<StarEngine::Shader> m_Shader;
	StarEngine::Ref<StarEngine::VertexArray> m_VertexArray;

	StarEngine::Ref<StarEngine::Shader> m_FlatColorShader;
	StarEngine::Ref<StarEngine::VertexArray> m_SquareVA;

	StarEngine::Ref<StarEngine::Texture2D> m_Texture, m_starLogoTexture;

	StarEngine::OrthographicCameraController m_CameraController;
	glm::vec3 m_SquareColor = { 0.2f, 0.3f, 0.8f };
};
