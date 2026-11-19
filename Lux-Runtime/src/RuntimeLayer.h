// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux.h"

#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Serialization/AssetPack.h"
#include "Lux/VR/VRSession.h"

#include <filesystem>

namespace Lux
{    
	class Project;

	class RuntimeLayer : public Layer
	{
	public:
		// `enableVR`: render through a VRSession (--vr, or VR in RuntimeSettings.yaml).
		RuntimeLayer(std::filesystem::path projectPath, bool enableVR);
		virtual ~RuntimeLayer() = default;

		void OnAttach() override;
		void OnDetach() override;
		// The game ends here, with the script host still running: scripts get OnDestroy and Application.Quitting.
		void OnApplicationQuit() override;
		void OnUpdate(Timestep ts) override;
		void OnEvent(Event& event) override;
		void OnImGuiRender() override;
		void OnRenderThreadIdle() override;

	private:
		bool OpenProject();
		bool LoadScene(AssetHandle sceneHandle);
		// A script asked for another scene (C# Scene.Load): swaps it in, carrying persistent entities over. Call it only while
		// the render thread is idle (OnRenderThreadIdle), because the old scene's GPU resources are freed.
		void ApplyRequestedScene();

		void OnScenePlay();
		void OnSceneStop();
		void CreateSwapChainResources();
		void RecreateSwapChainResources(uint32_t width, uint32_t height);
		void SubmitFinalImageToSwapChain();
		void SubmitCommandBufferToSwapChain();

		void DrawRuntimeOverlay();
		// Both draw downwards from `position` and leave it below their last line.
		void DrawDebugStats(glm::vec2& position);
		void DrawVRStats(glm::vec2& position);
		void DrawVersionInfo();
		void DrawString(const std::string& text, const glm::vec2& position, const glm::vec4& color = glm::vec4(1.0f), float size = 24.0f, bool shadow = true);
		void UpdateAudioVerification();
		void UpdateFPSStat();
		void UpdatePerformanceTimers();
		bool ShouldShowIntroVersion() const;

	private:
		std::filesystem::path m_ProjectPath;
		std::filesystem::path m_AudioVerificationPath;
		uint32_t m_AudioVerificationFrames = 0;
		// LUX_RUNTIME_CAPTURE=<file> writes the final frame as an LXIM image (tests/rendering/golden_compare.py --to-png)
		// after LUX_RUNTIME_CAPTURE_FRAME frames (default 120), then closes the game.
		std::filesystem::path m_CapturePath;
		uint32_t m_CaptureFrame = 120;
		uint32_t m_CaptureFrames = 0;
		std::shared_ptr<std::atomic<int>> m_CaptureState; // 0 idle or waiting, 1 written, -1 failed
		void UpdateCapture();
		uint32_t m_FramesSinceStart = 0;

		Ref<Project> m_RuntimeProject;
		Ref<Scene> m_RuntimeScene;
		Ref<SceneRenderer> m_SceneRenderer;
		Ref<Renderer2D> m_Renderer2D;
		Ref<AssetPack> m_AssetPack;

		bool m_EnableVR = false;
		Ref<VRSession> m_VRSession;

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
		bool m_ShowAudioAccessibility = false;
		bool m_AccessibilityWasPaused = false;
		CursorMode m_AccessibilityCursor = CursorMode::Normal;
	};
}
