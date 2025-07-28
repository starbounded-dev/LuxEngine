#pragma once

#include "StarEngine/ImGui/ImGuiLayer.h"
#include "StarEngine/Renderer/RenderCommandBuffer.h"

namespace StarEngine {

	class VulkanImGuiLayer : public ImGuiLayer
	{
	public:
		VulkanImGuiLayer();
		VulkanImGuiLayer(const std::string& name);
		virtual ~VulkanImGuiLayer();

		void Begin();
		void End();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;
	private:
		Ref<RenderCommandBuffer> m_RenderCommandBuffer;
		float m_Time = 0.0f;
	};

}
