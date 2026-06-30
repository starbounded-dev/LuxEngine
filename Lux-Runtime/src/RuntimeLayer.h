#pragma once

#include "Lux.h"

#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Serialization/AssetPack.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Lux
{    
	class Project;

	struct RuntimeBenchmarkConfig
	{
		bool Enabled = false;
		std::string Name;
		uint32_t WarmupFrames = 120;
		uint32_t CaptureFrames = 600;
		std::filesystem::path OutputPath = "RuntimeBenchmark-Sponza.csv";
	};

	class RuntimeLayer : public Layer
	{
	public:
		explicit RuntimeLayer(std::filesystem::path projectPath, RuntimeBenchmarkConfig benchmarkConfig = {});
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
		bool ShouldShowVersionInfo() const;
		void ApplyBenchmarkRendererSettings();
		void UpdateBenchmarkCamera();
		void UpdateBenchmark(Timestep ts);
		void FinishBenchmark();
		void WriteBenchmarkResults() const;

	private:
		struct BenchmarkFrameSample
		{
			uint32_t Frame = 0;
			float FrameTimeMS = 0.0f;
			float CPUTimeMS = 0.0f;
			float GPUTimeMS = 0.0f;
			uint32_t RenderGraphPassCount = 0;
			uint32_t DrawCalls = 0;
			uint32_t IndirectDraws = 0;
			uint32_t VisibleInstances = 0;
			float RenderScale = 1.0f;
			bool SSR = false;
			bool GTAO = false;
			bool Bloom = false;
			bool TAA = false;
			bool DOF = false;
			bool JumpFlood = false;
		};

		std::filesystem::path m_ProjectPath;
		RuntimeBenchmarkConfig m_BenchmarkConfig;
		std::vector<BenchmarkFrameSample> m_BenchmarkSamples;
		uint32_t m_BenchmarkFrameIndex = 0;

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
		bool m_ShowVersionInfo = false;
		bool m_BenchmarkComplete = false;
	};
}
