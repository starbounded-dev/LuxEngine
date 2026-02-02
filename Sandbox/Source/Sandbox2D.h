#pragma once

#include "Lux.h"
#include "Lux/Renderer/OrthographicCameraController.h"

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
	Lux::OrthographicCameraController m_CameraController;

	bool m_VSync = true;

	// Temp
	Lux::Ref<Lux::VertexArray> m_SquareVA;
	Lux::Ref<Lux::Shader> m_FlatColorShader;

	Lux::Ref<Lux::Texture2D> m_CheckerboardTexture;

	glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
};
