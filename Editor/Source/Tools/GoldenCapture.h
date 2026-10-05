// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Renderer/SceneRenderer.h"

#include <glm/glm.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace Lux {

	class EditorCamera;
	class Scene;
	class Viewport;

	// Deterministic frame capture for renderer regression tests ("golden images"). It is inert unless
	// LUX_GOLDEN_DIR is set, in which case the editor opens one scene, renders it with a fixed camera,
	// resolution and option set, waits for asset streaming to go idle, writes the final image and
	// averaged timings to LUX_GOLDEN_DIR, and closes.
	//
	// Environment (see .claude/docs/Rendering.md § Golden image capture):
	//   LUX_GOLDEN_DIR          output directory; unset = the tool does nothing
	//   LUX_GOLDEN_SCENE        scene path relative to the project's asset directory (required)
	//   LUX_GOLDEN_PROJECT      .luxproj to open first (optional; default: whatever the editor opens)
	//   LUX_GOLDEN_NAME         output file stem (default: the scene file stem)
	//   LUX_GOLDEN_SIZE         render size, "WIDTHxHEIGHT" (default 1280x720)
	//   LUX_GOLDEN_FRAME        minimum frames after the scene opens before capturing (default 300)
	//   LUX_GOLDEN_IDLE_FRAMES  consecutive frames with no pending asset loads required (default 60)
	//   LUX_GOLDEN_TIMEOUT      frames after which a capture still waiting on loads fails (default 6000)
	//   LUX_GOLDEN_PERF_FRAMES  frames averaged into <name>.perf.json after the capture (default 300)
	//   LUX_GOLDEN_OPTIONS      comma-separated Name=Value renderer overrides, e.g. "SSR=0,GTAO=1"
	//   LUX_GOLDEN_SELFTEST     "1" also runs RenderGraph::RunValidationSelfTests
	//   LUX_GOLDEN_EXIT         "0" keeps the editor open afterwards (default: close)
	//
	// Threading: every member runs on the main thread from EditorLayer::OnUpdate, except the image
	// readback and file write, which run inside a Renderer::Submit lambda on the render thread and
	// report back through CaptureResult's atomics.
	class GoldenCapture
	{
	public:
		struct Hooks
		{
			std::function<void(const std::filesystem::path&)> OpenProject;
			std::function<bool(const std::filesystem::path&)> OpenScene;   // absolute scene file path
		};

		// Null unless LUX_GOLDEN_DIR is set.
		static Scope<GoldenCapture> CreateFromEnvironment(Hooks hooks);

		explicit GoldenCapture(Hooks hooks);

		// Call at the top of EditorLayer::OnUpdate, before anything that needs a project.
		void BeginFrame();
		// Call before Viewport::SyncSceneViewport: pins the render resolution.
		void ConfigureViewport(Viewport& viewport);
		// Call after the editor applied its per-frame renderer options, before rendering.
		void ConfigureFrame(Scene& scene, EditorCamera& camera, SceneRenderer& renderer);
		// Call after the frame's render work has been submitted.
		void EndFrame(const Ref<SceneRenderer>& renderer);

		// Editor overlays (icons, gizmos, audio visualisation) depend on user preferences and
		// animate, so a capture leaves them out.
		bool SuppressOverlays() const { return true; }

	private:
		enum class Stage
		{
			OpenProject,
			OpenScene,
			WarmUp,
			WaitForReadback,
			Perf,
			Done
		};

		struct CaptureResult : public RefCounted
		{
			std::atomic<bool> Finished{ false };
			std::atomic<bool> Succeeded{ false };
		};

		struct PassTiming
		{
			double CPUTime = 0.0;
			double GPUTime = 0.0;
			uint32_t Samples = 0;
		};

		void ParseOptions(const std::string& options);
		void ApplyOptions(SceneRenderer& renderer) const;
		void ComputeCameraPose(Scene& scene);
		void RequestReadback(Ref<SceneRenderer> renderer);
		void AccumulatePerf(const SceneRenderer& renderer);
		void WritePerf() const;
		void RunSelfTest() const;
		void Finish(bool success, const std::string& reason);

	private:
		Hooks m_Hooks;

		std::filesystem::path m_OutputDirectory;
		std::filesystem::path m_ProjectPath;
		std::filesystem::path m_ScenePath;
		std::string m_Name;
		glm::uvec2 m_Size = { 1280, 720 };
		uint32_t m_MinFrames = 300;
		uint32_t m_IdleFramesRequired = 60;
		uint32_t m_TimeoutFrames = 6000;
		uint32_t m_PerfFrames = 300;
		bool m_RunSelfTest = false;
		bool m_ExitWhenDone = true;
		std::map<std::string, std::string> m_Options;

		Stage m_Stage = Stage::OpenProject;
		uint32_t m_SceneFrames = 0;
		uint32_t m_IdleFrames = 0;
		uint32_t m_PerfSamples = 0;

		struct CameraPose
		{
			glm::vec3 FocalPoint = { 0.0f, 2.0f, 0.0f };
			float Distance = 20.0f;
			float Pitch = 0.35f;
			float Yaw = -0.6f;
		} m_CameraPose;

		Ref<CaptureResult> m_Result;

		double m_GPUTime = 0.0;
		double m_CPUTime = 0.0;
		double m_MainThreadWorkTime = 0.0;
		double m_RenderThreadWorkTime = 0.0;
		std::map<std::string, PassTiming> m_PassTimings;
	};

}
