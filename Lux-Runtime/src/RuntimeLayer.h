#pragma once

#include "Lux.h"

#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Serialization/AssetPack.h"

#include <filesystem>

namespace Lux
{    
	class Project;

	class RuntimeLayer : public Layer
	{
	public:
		explicit RuntimeLayer(std::filesystem::path projectPath);
		virtual ~RuntimeLayer() = default;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;
		void OnEvent(Event& event) override;

	private:
		bool OpenProject();
		bool LoadScene(AssetHandle sceneHandle);

		void OnScenePlay();
		void OnSceneStop();
		void CreateSwapChainResources();
		void RecreateSwapChainResources(uint32_t width, uint32_t height);
		void SubmitFinalImageToSwapChain();
		void SubmitCommandBufferToSwapChain();

		void DrawRuntimeOverlay();
		void DrawDebugStats();
		void DrawVersionInfo();
		void DrawString(const std::string& text, const glm::vec2& position, const glm::vec4& color = glm::vec4(1.0f), float size = 24.0f, bool shadow = true);
		void UpdateFPSStat();
		void UpdatePerformanceTimers();
		bool ShouldShowIntroVersion() const;

	private:
		std::filesystem::path m_ProjectPath;

		Ref<Project> m_RuntimeProject;
		Ref<Scene> m_RuntimeScene;
		Ref<SceneRenderer> m_SceneRenderer;
		Ref<Renderer2D> m_Renderer2D;
		Ref<AssetPack> m_AssetPack;

		Ref<RenderCommandBuffer> m_CommandBuffer;
		Ref<Framebuffer> m_SwapChainFramebuffer;
		Ref<RenderPass> m_SwapChainRenderPass;
		Ref<Material> m_SwapChainMaterial;

		glm::mat4 m_Renderer2DProjection{ 1.0f };

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_FramesPerSecond = 0;
		float m_UpdateFPSTimer = 0.0f;
		float m_UpdatePerformanceTimer = 0.0f;
		float m_FrameTime = 0.0f;
		float m_CPUTime = 0.0f;
		float m_GPUTime = 0.0f;
		float m_IntroVersionDuration = 5.0f;

		bool m_SceneRunning = false;
		bool m_ShowDebugDisplay = false;
	};
}
