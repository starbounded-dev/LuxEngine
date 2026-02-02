#pragma once

#include "Lux.h"
#include "Lux/Renderer/OrthographicCameraController.h"

class ExampleLayer : public Lux::Layer
{

public:
	ExampleLayer();
	virtual ~ExampleLayer() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	void OnUpdate(Lux::Timestep ts) override;
	virtual void OnImGuiRender() override;
	void OnEvent(Lux::Event& e) override;
private:
	Lux::ShaderLibrary m_ShaderLibrary;
	Lux::Ref<Lux::Shader> m_Shader;
	Lux::Ref<Lux::VertexArray> m_VertexArray;

	Lux::Ref<Lux::Shader> m_FlatColorShader;
	Lux::Ref<Lux::VertexArray> m_SquareVA;

	Lux::Ref<Lux::Texture2D> m_Texture, m_LuxLogoTexture;

	Lux::OrthographicCameraController m_CameraController;
	glm::vec3 m_SquareColor = { 0.2f, 0.3f, 0.8f };
};
