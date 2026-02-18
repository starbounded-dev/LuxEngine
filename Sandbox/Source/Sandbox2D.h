#pragma once

#include "Lux.h"

namespace Lux {
	class RenderCommandBuffer;
}

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
	Lux::EditorCamera m_EditorCamera;

	Lux::Ref<Lux::Renderer2D> m_Renderer2D;

	glm::mat4 m_Renderer2DProj{ 1.0f };

	bool m_VSync = true;

	Lux::Ref<Lux::RenderCommandBuffer> m_CommandBuffer;
	Lux::Ref<Lux::Texture2D> m_CheckerboardTexture;

	glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
};
