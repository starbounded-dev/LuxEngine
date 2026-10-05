// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "GoldenCapture.h"

#include "Viewport/Viewport.h"

#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Core/Application.h"
#include "Lux/Core/Events/ApplicationEvent.h"
#include "Lux/Editor/EditorCamera.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Image.h"
#include "Lux/Renderer/RenderGraph.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Utilities/FileSystem.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>

namespace Lux {

	namespace {

		// Matches tests/rendering/golden_compare.py. Little-endian, 32 bytes, followed by
		// Width * Height * BytesPerPixel texel bytes with no row padding.
		struct GoldenImageHeader
		{
			char Magic[4] = { 'L', 'X', 'I', 'M' };
			uint32_t Version = 1;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Channels = 0;
			uint32_t ComponentType = 0;
			uint32_t ImageFormat = 0;
			uint32_t BytesPerPixel = 0;
		};
		static_assert(sizeof(GoldenImageHeader) == 32);

		enum class GoldenComponentType : uint32_t
		{
			UNorm8 = 0,
			Float16 = 1,
			Float32 = 2,
			UInt8 = 3,
			UInt16 = 4,
			UInt32 = 5,
			Raw = 255 // compared byte for byte
		};

		constexpr float k_CameraOrbitDistance = 10.0f;

		// LUX_GOLDEN_OPTIONS names that map straight onto a SceneRendererOptions flag. Bloom, DOF and
		// Colliders are handled separately in GoldenCapture::ApplyOptions.
		struct BoolOption
		{
			std::string_view Name;
			bool SceneRendererOptions::* Member;
		};

		constexpr BoolOption k_BoolOptions[] = {
			{ "Grid", &SceneRendererOptions::ShowGrid },
			{ "SSR", &SceneRendererOptions::EnableSSR },
			{ "GTAO", &SceneRendererOptions::EnableGTAO },
			{ "SMAA", &SceneRendererOptions::EnableSMAA },
			{ "JumpFlood", &SceneRendererOptions::EnableJumpFlood },
			{ "DebugCategories", &SceneRendererOptions::ShowDebugCategories },
			{ "VRS", &SceneRendererOptions::EnableVariableRateShading },
			{ "MeshShaders", &SceneRendererOptions::EnableMeshShaders },
			{ "AsyncCompute", &SceneRendererOptions::EnableAsyncCompute },
			{ "OcclusionCulling", &SceneRendererOptions::EnableOcclusionCulling },
			{ "SoftShadows", &SceneRendererOptions::SoftShadows },
		};

		constexpr std::string_view k_SpecialOptions[] = { "Bloom", "DOF", "Colliders" };

		bool IsKnownOption(std::string_view name)
		{
			for (const BoolOption& option : k_BoolOptions)
			{
				if (option.Name == name)
					return true;
			}
			return std::find(std::begin(k_SpecialOptions), std::end(k_SpecialOptions), name) != std::end(k_SpecialOptions);
		}

		struct FormatLayout
		{
			uint32_t Channels;
			GoldenComponentType Type;
		};

		FormatLayout DescribeFormat(ImageFormat format)
		{
			switch (format)
			{
				case ImageFormat::RED8UN:  return { 1, GoldenComponentType::UNorm8 };
				case ImageFormat::RED8UI:  return { 1, GoldenComponentType::UInt8 };
				case ImageFormat::RED16UI: return { 1, GoldenComponentType::UInt16 };
				case ImageFormat::RED32UI: return { 1, GoldenComponentType::UInt32 };
				case ImageFormat::RG32UI:  return { 2, GoldenComponentType::UInt32 };
				case ImageFormat::RED32F:  return { 1, GoldenComponentType::Float32 };
				case ImageFormat::RG8:     return { 2, GoldenComponentType::UNorm8 };
				case ImageFormat::RG16F:   return { 2, GoldenComponentType::Float16 };
				case ImageFormat::RG32F:   return { 2, GoldenComponentType::Float32 };
				case ImageFormat::RGBA:
				case ImageFormat::SRGBA:   return { 4, GoldenComponentType::UNorm8 };
				case ImageFormat::RGBA16F: return { 4, GoldenComponentType::Float16 };
				case ImageFormat::RGBA32F: return { 4, GoldenComponentType::Float32 };
				default:                   return { Utils::GetImageFormatBPP(format), GoldenComponentType::Raw };
			}
		}

		std::string GetEnv(const char* name)
		{
			return FileSystem::HasEnvironmentVariable(name) ? FileSystem::GetEnvironmentVariable(name) : std::string{};
		}

		uint32_t GetEnvUInt(const char* name, uint32_t fallback)
		{
			const std::string value = GetEnv(name);
			uint32_t result = fallback;
			if (!value.empty())
				std::from_chars(value.data(), value.data() + value.size(), result);
			return result;
		}

		bool ParseBool(const std::string& value)
		{
			return value == "1" || value == "true" || value == "on";
		}

	}

	Scope<GoldenCapture> GoldenCapture::CreateFromEnvironment(Hooks hooks)
	{
		if (GetEnv("LUX_GOLDEN_DIR").empty())
			return nullptr;

		return CreateScope<GoldenCapture>(std::move(hooks));
	}

	GoldenCapture::GoldenCapture(Hooks hooks)
		: m_Hooks(std::move(hooks)), m_Result(Ref<CaptureResult>::Create())
	{
		m_OutputDirectory = GetEnv("LUX_GOLDEN_DIR");
		m_ProjectPath = GetEnv("LUX_GOLDEN_PROJECT");
		m_ScenePath = GetEnv("LUX_GOLDEN_SCENE");
		m_Name = GetEnv("LUX_GOLDEN_NAME");
		if (m_Name.empty())
			m_Name = m_ScenePath.stem().string();

		const std::string size = GetEnv("LUX_GOLDEN_SIZE");
		if (const size_t separator = size.find('x'); separator != std::string::npos)
		{
			uint32_t width = 0, height = 0;
			std::from_chars(size.data(), size.data() + separator, width);
			std::from_chars(size.data() + separator + 1, size.data() + size.size(), height);
			if (width > 0 && height > 0)
				m_Size = { width, height };
		}

		m_MinFrames = GetEnvUInt("LUX_GOLDEN_FRAME", m_MinFrames);
		m_IdleFramesRequired = GetEnvUInt("LUX_GOLDEN_IDLE_FRAMES", m_IdleFramesRequired);
		m_TimeoutFrames = GetEnvUInt("LUX_GOLDEN_TIMEOUT", m_TimeoutFrames);
		m_PerfFrames = GetEnvUInt("LUX_GOLDEN_PERF_FRAMES", m_PerfFrames);
		m_RunSelfTest = ParseBool(GetEnv("LUX_GOLDEN_SELFTEST"));
		m_ExitWhenDone = GetEnv("LUX_GOLDEN_EXIT") != "0";
		ParseOptions(GetEnv("LUX_GOLDEN_OPTIONS"));

		FileSystem::CreateDirectory(m_OutputDirectory);

		// Uncapped presentation so the perf numbers measure the frame, not the display. Applied by
		// the window at its next safe point; the user's saved preferences are not touched.
		Application::Get().GetWindow().SetVSync(false);
		Application::Get().GetWindow().SetPreferImmediatePresentMode(true);
		Application::Get().SetTargetFrameRate(0);

		LUX_CORE_INFO_TAG("Editor", "[GoldenCapture] scene '{}', {}x{}, output '{}'", m_ScenePath.string(), m_Size.x, m_Size.y, m_OutputDirectory.string());
	}

	void GoldenCapture::ParseOptions(const std::string& options)
	{
		size_t begin = 0;
		while (begin < options.size())
		{
			size_t end = options.find(',', begin);
			if (end == std::string::npos)
				end = options.size();

			const std::string entry = options.substr(begin, end - begin);
			const size_t equals = entry.find('=');
			const std::string name = entry.substr(0, equals);
			if (equals == std::string::npos)
			{
				if (!entry.empty())
					LUX_CORE_ERROR_TAG("Editor", "[GoldenCapture] option '{}' has no value", entry);
			}
			else if (!IsKnownOption(name))
			{
				LUX_CORE_ERROR_TAG("Editor", "[GoldenCapture] unknown option '{}'", name);
			}
			else
			{
				m_Options[name] = entry.substr(equals + 1);
			}

			begin = end + 1;
		}
	}

	void GoldenCapture::ApplyOptions(SceneRenderer& renderer) const
	{
		SceneRendererOptions& options = renderer.GetOptions();

		// Fixed regardless of user settings: no editor grid, no colliders, native resolution
		// (dynamic resolution follows GPU time and would make every run different).
		options.ShowGrid = false;
		options.ShowPhysicsColliders = false;
		options.ShowSelectedInWireframe = false;
		options.ShowDebugCategories = false;
		options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;

		for (const auto& [name, value] : m_Options)
		{
			const bool enabled = ParseBool(value);
			for (const BoolOption& option : k_BoolOptions)
			{
				if (option.Name == name)
					options.*option.Member = enabled;
			}

			if (name == "Bloom")
			{
				renderer.GetBloomSettings().Enabled = enabled;
			}
			else if (name == "DOF")
			{
				renderer.GetDOFSettings().Enabled = enabled;
			}
			else if (name == "Colliders")
			{
				options.ShowPhysicsColliders = enabled;
				options.PhysicsColliderMode = SceneRendererOptions::PhysicsColliderView::All;
			}
		}
	}

	void GoldenCapture::BeginFrame()
	{
		switch (m_Stage)
		{
			case Stage::OpenProject:
			{
				if (!m_ProjectPath.empty())
				{
					const Ref<Project> active = Project::GetActive();
					std::error_code error;
					if (!active || !std::filesystem::equivalent(active->GetProjectFilePath(), m_ProjectPath, error))
						m_Hooks.OpenProject(m_ProjectPath);
				}
				m_Stage = Stage::OpenScene;
				break;
			}
			case Stage::OpenScene:
			{
				if (!Project::GetActive())
				{
					Finish(false, "no project is open (set LUX_GOLDEN_PROJECT)");
					break;
				}
				if (m_ScenePath.empty())
				{
					Finish(false, "LUX_GOLDEN_SCENE is not set");
					break;
				}

				const std::filesystem::path absoluteScene = Project::GetActiveAssetDirectory() / m_ScenePath;
				if (!m_Hooks.OpenScene(absoluteScene))
				{
					Finish(false, std::format("could not open scene '{}'", absoluteScene.string()));
					break;
				}
				m_Stage = Stage::WarmUp;
				break;
			}
			default:
				break;
		}
	}

	void GoldenCapture::ConfigureViewport(Viewport& viewport)
	{
		viewport.SetSize({ (float)m_Size.x, (float)m_Size.y });
	}

	void GoldenCapture::ConfigureFrame(Scene& scene, EditorCamera& camera, SceneRenderer& renderer)
	{
		if (m_Stage == Stage::OpenProject || m_Stage == Stage::OpenScene)
			return;

		if (m_SceneFrames == 0)
		{
			ComputeCameraPose(scene);
			if (scene.GetPostProcessSettings().ExposureControl == ExposureMode::Automatic)
				LUX_CORE_WARN_TAG("Editor", "[GoldenCapture] the scene uses automatic exposure; its adaptation follows frame time, so captures may differ between runs");
		}

		// Re-applied every frame: nothing the user does with the mouse may move the capture.
		camera.SetActive(false);
		camera.SetOrbitState(m_CameraPose.FocalPoint, m_CameraPose.Distance, m_CameraPose.Pitch, m_CameraPose.Yaw);
		ApplyOptions(renderer);
	}

	void GoldenCapture::ComputeCameraPose(Scene& scene)
	{
		Entity cameraEntity = scene.GetPrimaryCameraEntity();
		if (!cameraEntity)
		{
			LUX_CORE_WARN_TAG("Editor", "[GoldenCapture] scene has no primary camera; using the default orbit pose");
			return;
		}

		// EditorCamera orientation is glm::quat(vec3(-pitch, -yaw, 0)) and it looks down -Z, so a
		// camera entity's forward vector F gives pitch = -asin(F.y), yaw = -atan2(-F.x, -F.z).
		const TransformComponent transform = scene.GetWorldSpaceTransform(cameraEntity);
		const glm::vec3 forward = glm::normalize(glm::rotate(transform.GetRotation(), glm::vec3(0.0f, 0.0f, -1.0f)));

		m_CameraPose.Pitch = -std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
		m_CameraPose.Yaw = -std::atan2(-forward.x, -forward.z);
		m_CameraPose.Distance = k_CameraOrbitDistance;
		m_CameraPose.FocalPoint = transform.Translation + forward * k_CameraOrbitDistance;

		const glm::vec3 check = glm::rotate(glm::quat(glm::vec3(-m_CameraPose.Pitch, -m_CameraPose.Yaw, 0.0f)), glm::vec3(0.0f, 0.0f, -1.0f));
		if (glm::length(check - forward) > 0.01f)
			LUX_CORE_WARN_TAG("Editor", "[GoldenCapture] camera pose does not reproduce the scene camera's direction exactly; the capture is still deterministic");
	}

	void GoldenCapture::EndFrame(const Ref<SceneRenderer>& renderer)
	{
		switch (m_Stage)
		{
			case Stage::WarmUp:
			{
				m_SceneFrames++;

				const Ref<EditorAssetManager> assetManager = Project::GetEditorAssetManager();
				const bool streamingIdle = !assetManager || assetManager->GetPendingAsyncLoadCount() == 0;
				m_IdleFrames = streamingIdle ? m_IdleFrames + 1 : 0;

				if (m_SceneFrames >= m_MinFrames && m_IdleFrames >= m_IdleFramesRequired && renderer && renderer->IsReady())
				{
					RequestReadback(renderer);
					m_Stage = Stage::WaitForReadback;
				}
				else if (m_SceneFrames >= m_TimeoutFrames)
				{
					Finish(false, std::format("asset streaming never went idle within {} frames", m_TimeoutFrames));
				}
				break;
			}
			case Stage::WaitForReadback:
			{
				if (!m_Result->Finished.load(std::memory_order_acquire))
					break;

				if (!m_Result->Succeeded.load(std::memory_order_relaxed))
				{
					Finish(false, "final image readback failed");
					break;
				}

				if (m_RunSelfTest)
					RunSelfTest();
				m_Stage = Stage::Perf;
				break;
			}
			case Stage::Perf:
			{
				if (renderer)
					AccumulatePerf(*renderer);
				if (m_PerfSamples >= m_PerfFrames)
				{
					WritePerf();
					Finish(true, {});
				}
				break;
			}
			default:
				break;
		}
	}

	void GoldenCapture::RequestReadback(Ref<SceneRenderer> renderer)
	{
		Ref<Image2D> image = renderer->GetFinalPassImage();
		if (!image)
		{
			Finish(false, "the scene renderer has no final image");
			return;
		}

		const std::filesystem::path path = m_OutputDirectory / (m_Name + ".lximg");
		Ref<CaptureResult> result = m_Result;

		// Queued after this frame's scene rendering in the same submission queue, so the copy reads
		// the finished frame. CopyToHostBuffer records, submits and waits on its own command list.
		Renderer::Submit([image, path, result]() mutable
		{
			Buffer pixels;
			image->CopyToHostBuffer(pixels);

			const ImageSpecification& spec = image->GetSpecification();
			GoldenImageHeader header;
			header.Width = spec.Width;
			header.Height = spec.Height;
			header.ImageFormat = (uint32_t)spec.Format;
			header.BytesPerPixel = Utils::GetImageFormatBPP(spec.Format);
			const FormatLayout layout = DescribeFormat(spec.Format);
			header.Channels = layout.Channels;
			header.ComponentType = (uint32_t)layout.Type;

			const uint64_t expectedSize = (uint64_t)header.Width * header.Height * header.BytesPerPixel;
			bool succeeded = pixels && pixels.Size >= expectedSize;
			if (succeeded)
			{
				std::ofstream file(path, std::ios::binary | std::ios::trunc);
				file.write(reinterpret_cast<const char*>(&header), sizeof(header));
				file.write(static_cast<const char*>(pixels.Data), (std::streamsize)expectedSize);
				succeeded = file.good();
			}
			pixels.Release();

			if (succeeded)
				LUX_CORE_INFO_TAG("Editor", "[GoldenCapture] wrote {} ({}x{}, format {})", path.string(), header.Width, header.Height, header.ImageFormat);

			result->Succeeded.store(succeeded, std::memory_order_relaxed);
			result->Finished.store(true, std::memory_order_release);
		});
	}

	void GoldenCapture::AccumulatePerf(const SceneRenderer& renderer)
	{
		const SceneRenderer::Statistics& stats = renderer.GetStatistics();
		const Application::PerformanceTimers& timers = Application::Get().GetPerformanceTimers();

		m_GPUTime += stats.TotalGPUTime;
		m_CPUTime += stats.TotalCPUTime;
		m_MainThreadWorkTime += timers.MainThreadWorkTime;
		m_RenderThreadWorkTime += timers.RenderThreadWorkTime;

		for (const SceneRenderer::PassProfile& profile : stats.PassProfiles)
		{
			if (!profile.Active || !profile.Name)
				continue;

			PassTiming& timing = m_PassTimings[profile.Name];
			timing.CPUTime += profile.CPUTime;
			timing.GPUTime += profile.GPUTime;
			timing.Samples++;
		}

		m_PerfSamples++;
	}

	void GoldenCapture::WritePerf() const
	{
		const double samples = glm::max(1.0, (double)m_PerfSamples);
		std::string json = "{\n";
		json += std::format("  \"scene\": \"{}\",\n", m_ScenePath.generic_string());
		json += std::format("  \"width\": {},\n  \"height\": {},\n", m_Size.x, m_Size.y);
		json += std::format("  \"frames\": {},\n", m_PerfSamples);
		json += std::format("  \"gpuMs\": {:.4f},\n", m_GPUTime / samples);
		json += std::format("  \"sceneRendererCpuMs\": {:.4f},\n", m_CPUTime / samples);
		json += std::format("  \"mainThreadMs\": {:.4f},\n", m_MainThreadWorkTime / samples);
		json += std::format("  \"renderThreadMs\": {:.4f},\n", m_RenderThreadWorkTime / samples);
		json += "  \"passes\": {";

		bool first = true;
		for (const auto& [name, timing] : m_PassTimings)
		{
			const double passSamples = glm::max(1.0, (double)timing.Samples);
			json += std::format("{}\n    \"{}\": {{ \"cpuMs\": {:.4f}, \"gpuMs\": {:.4f} }}", first ? "" : ",", name, timing.CPUTime / passSamples, timing.GPUTime / passSamples);
			first = false;
		}
		json += "\n  }\n}\n";

		std::ofstream file(m_OutputDirectory / (m_Name + ".perf.json"), std::ios::trunc);
		file << json;
	}

	void GoldenCapture::RunSelfTest() const
	{
		std::vector<std::string> failures;
		const bool passed = RenderGraph::RunValidationSelfTests(&failures);

		std::ofstream file(m_OutputDirectory / "rendergraph-selftest.txt", std::ios::trunc);
		file << (passed ? "PASS" : "FAIL") << "\n";
		for (const std::string& failure : failures)
			file << failure << "\n";

		if (passed)
			LUX_CORE_INFO_TAG("Editor", "[GoldenCapture] RenderGraph self-tests passed");
		else
			LUX_CORE_ERROR_TAG("Editor", "[GoldenCapture] RenderGraph self-tests failed ({} failures)", failures.size());
	}

	void GoldenCapture::Finish(bool success, const std::string& reason)
	{
		if (m_Stage == Stage::Done)
			return;
		m_Stage = Stage::Done;

		// golden_run.py reads this marker to tell a finished capture from a crash or timeout.
		std::ofstream file(m_OutputDirectory / (m_Name + ".status"), std::ios::trunc);
		file << (success ? "OK" : "FAILED") << "\n" << reason << "\n";

		if (success)
			LUX_CORE_INFO_TAG("Editor", "[GoldenCapture] finished '{}'", m_Name);
		else
			LUX_CORE_ERROR_TAG("Editor", "[GoldenCapture] failed: {}", reason);

		if (m_ExitWhenDone)
			Application::Get().QueueEvent([]() { Application::Get().DispatchEvent<WindowCloseEvent, true>(); });
	}

}
