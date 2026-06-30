#include "RuntimeLayer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Application.h"
#include "Lux/Core/Input.h"
#include "Lux/Project/Project.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Utilities/StringUtils.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <numeric>

namespace Lux
{
	namespace
	{
		constexpr const char* s_RuntimeProjectFile = "Project.luxruntime";
		constexpr const char* s_RuntimeAssetPackFile = "AssetPack.lap";

		std::filesystem::path ResolveRuntimeProjectFile(const std::filesystem::path& projectPath)
		{
			std::error_code ec;
			if (std::filesystem::is_regular_file(projectPath, ec))
				return projectPath;

			const std::filesystem::path assetDirectoryPath = projectPath / "Assets" / s_RuntimeProjectFile;
			if (std::filesystem::exists(assetDirectoryPath, ec))
				return assetDirectoryPath;

			return projectPath / s_RuntimeProjectFile;
		}

		float ResolveBenchmarkRenderScale(const SceneRendererOptions& options)
		{
			switch (options.ResolutionScaleMode)
			{
				case SceneRendererOptions::RenderResolutionScaleMode::Scale75:
					return 0.75f;
				case SceneRendererOptions::RenderResolutionScaleMode::Scale50:
					return 0.50f;
				case SceneRendererOptions::RenderResolutionScaleMode::Dynamic:
					return options.DynamicResolutionScale;
				case SceneRendererOptions::RenderResolutionScaleMode::Native:
				default:
					return 1.0f;
			}
		}

		std::filesystem::path ResolveBenchmarkCSVPath(const std::filesystem::path& requestedPath)
		{
			std::filesystem::path csvPath = requestedPath.empty() ? std::filesystem::path("RuntimeBenchmark-Sponza.csv") : requestedPath;
			if (csvPath.extension() == ".json")
				csvPath.replace_extension(".csv");
			else if (csvPath.extension().empty())
				csvPath += ".csv";
			return csvPath;
		}

		std::filesystem::path ResolveBenchmarkJSONPath(const std::filesystem::path& requestedPath)
		{
			std::filesystem::path jsonPath = requestedPath.empty() ? std::filesystem::path("RuntimeBenchmark-Sponza.json") : requestedPath;
			jsonPath.replace_extension(".json");
			return jsonPath;
		}

		void EnsureParentDirectory(const std::filesystem::path& path)
		{
			const std::filesystem::path parent = path.parent_path();
			if (parent.empty())
				return;

			std::error_code ec;
			std::filesystem::create_directories(parent, ec);
			if (ec)
				LUX_CORE_WARN("Could not create benchmark output directory '{}': {}", parent.string(), ec.message());
		}

		struct BenchmarkTimingSummary
		{
			float Average = 0.0f;
			float P95 = 0.0f;
			float P99 = 0.0f;
		};

		BenchmarkTimingSummary SummarizeTiming(std::vector<float> values)
		{
			BenchmarkTimingSummary summary;
			if (values.empty())
				return summary;

			summary.Average = std::accumulate(values.begin(), values.end(), 0.0f) / (float)values.size();
			std::sort(values.begin(), values.end());

			auto percentile = [&values](float p)
				{
					const size_t index = std::min(values.size() - 1, (size_t)std::ceil((values.size() * p) - 1.0f));
					return values[index];
				};

			summary.P95 = percentile(0.95f);
			summary.P99 = percentile(0.99f);
			return summary;
		}
	}

	RuntimeLayer::RuntimeLayer(std::filesystem::path projectPath, RuntimeBenchmarkConfig benchmarkConfig)
		: m_ProjectPath(std::move(projectPath)), m_BenchmarkConfig(std::move(benchmarkConfig))
	{
	}

	void RuntimeLayer::OnAttach()
	{
		if (!OpenProject())
		{
			Application::Get().Close();
			return;
		}

		SceneRendererSpecification rendererSpec;
		rendererSpec.ViewportWidth = Application::Get().GetWindow().GetWidth();
		rendererSpec.ViewportHeight = Application::Get().GetWindow().GetHeight();

		m_SceneRenderer = Ref<SceneRenderer>::Create(m_RuntimeScene, rendererSpec);
		m_SceneRenderer->ApplyProjectSettings(m_RuntimeProject->GetConfig().SceneRenderer);
		m_SceneRenderer->GetOptions().ShowGrid = false;
		m_SceneRenderer->SetDebugViewMode(SceneRenderer::DebugViewMode::Final);
		ApplyBenchmarkRendererSettings();

		m_Renderer2D = Ref<Renderer2D>::Create();
		m_Renderer2D->SetLineWidth(2.0f);

		CreateSwapChainResources();
		OnScenePlay();
	}

	void RuntimeLayer::OnDetach()
	{
		OnSceneStop();

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(nullptr);

		m_RuntimeScene = nullptr;
		m_RuntimeProject = nullptr;
		m_AssetPack = nullptr;
		Project::SetActiveRuntime(nullptr, nullptr);
	}

	bool RuntimeLayer::OpenProject()
	{
		const std::filesystem::path runtimeProjectFile = ResolveRuntimeProjectFile(m_ProjectPath);
		const std::filesystem::path assetPackFile = runtimeProjectFile.parent_path() / s_RuntimeAssetPackFile;

		std::error_code ec;
		if (!std::filesystem::exists(runtimeProjectFile, ec))
		{
			LUX_CORE_ERROR("Runtime project file not found: {}", runtimeProjectFile.string());
			return false;
		}

		if (!std::filesystem::exists(assetPackFile, ec))
		{
			LUX_CORE_ERROR("Runtime asset pack not found: {}", assetPackFile.string());
			return false;
		}

		m_AssetPack = AssetPack::Load(assetPackFile);
		if (!m_AssetPack)
			return false;

		m_RuntimeProject = Project::LoadRuntime(runtimeProjectFile, m_AssetPack);
		if (!m_RuntimeProject)
			return false;

		if (!LoadScene(m_RuntimeProject->GetConfig().StartSceneHandle))
			return false;

		const std::string scriptModuleName = m_RuntimeProject->GetConfig().ScriptModulePath.filename().string();
		const bool startupSceneUsesScripts = m_RuntimeScene && m_RuntimeScene->HasScripts();
		bool scriptsLoaded = false;

		if (m_AssetPack->HasAppBinary())
		{
			Buffer appBinary = m_AssetPack->ReadAppBinary();
			scriptsLoaded = ScriptEngine::Init(appBinary, scriptModuleName);
			appBinary.Release();
		}
		else
		{
			scriptsLoaded = ScriptEngine::Init();
		}

		if (!scriptsLoaded && startupSceneUsesScripts)
		{
			LUX_CORE_ERROR("Failed to load game scripts: {}", scriptModuleName.empty() ? "App.dll" : scriptModuleName);
			return false;
		}

		return true;
	}

	bool RuntimeLayer::LoadScene(AssetHandle sceneHandle)
	{
		if (!sceneHandle)
		{
			LUX_CORE_ERROR("Runtime project does not have a start scene.");
			return false;
		}

		Ref<Scene> scene = Project::GetRuntimeAssetManager()->LoadScene(sceneHandle);
		if (!scene)
		{
			LUX_CORE_ERROR("Failed to load runtime scene {}", (uint64_t)sceneHandle);
			return false;
		}

		m_RuntimeScene = scene;
		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_RuntimeScene);

		const std::string& applicationName = Application::Get().GetSpecification().Name;
		Application::Get().GetWindow().SetTitle(applicationName.empty() ? "Lux Runtime" : applicationName);
		return true;
	}

	void RuntimeLayer::OnScenePlay()
	{
		if (!m_RuntimeScene || m_SceneRunning)
			return;

		m_RuntimeScene->OnRuntimeStart();
		m_SceneRunning = true;
	}

	void RuntimeLayer::OnSceneStop()
	{
		if (!m_RuntimeScene || !m_SceneRunning)
			return;

		m_RuntimeScene->OnRuntimeStop();
		m_SceneRunning = false;
	}

	void RuntimeLayer::CreateSwapChainResources()
	{
		FramebufferSpecification framebufferSpec;
		framebufferSpec.DebugName = "RuntimeSwapChain";
		framebufferSpec.SwapChainTarget = true;
		framebufferSpec.ClearColor = { 0.01f, 0.01f, 0.012f, 1.0f };
		framebufferSpec.Attachments = { ImageFormat::RGBA };
		m_SwapChainFramebuffer = Framebuffer::Create(framebufferSpec);

		PipelineSpecification pipelineSpec;
		pipelineSpec.Layout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};
		pipelineSpec.BackfaceCulling = false;
		pipelineSpec.DepthTest = false;
		pipelineSpec.DepthWrite = false;
		pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("TexturePass");
		pipelineSpec.TargetFramebuffer = m_SwapChainFramebuffer;
		pipelineSpec.DebugName = "RuntimeSwapChain";

		RenderPassSpecification renderPassSpec;
		renderPassSpec.DebugName = "RuntimeSwapChain";
		renderPassSpec.Pipeline = Pipeline::Create(pipelineSpec);
		m_SwapChainRenderPass = RenderPass::Create(renderPassSpec);
		m_SwapChainRenderPass->Bake();

		m_SwapChainMaterial = Material::Create(pipelineSpec.Shader, "RuntimeSwapChain");
		m_CommandBuffer = RenderCommandBuffer::Create(0, "RuntimeSwapChain", false);
	}

	void RuntimeLayer::RecreateSwapChainResources(uint32_t width, uint32_t height)
	{
		if (!m_SwapChainFramebuffer)
			return;

		m_SwapChainFramebuffer->Resize(width, height, true);
		if (m_SwapChainRenderPass && m_SwapChainRenderPass->GetPipeline())
			m_SwapChainRenderPass->GetPipeline()->Invalidate();
	}

	void RuntimeLayer::OnUpdate(Timestep ts)
	{
		if (!m_RuntimeScene || !m_SceneRenderer)
			return;

		AssetManager::SyncWithAssetThread();

		m_UpdateFPSTimer -= ts;
		if (m_UpdateFPSTimer <= 0.0f)
		{
			UpdateFPSStat();
			m_UpdateFPSTimer = 1.0f;
		}

		m_UpdatePerformanceTimer -= ts;
		if (m_UpdatePerformanceTimer <= 0.0f)
		{
			UpdatePerformanceTimers();
			m_UpdatePerformanceTimer = 0.2f;
		}

		auto [width, height] = Application::Get().GetWindow().GetSize();
		if (width == 0 || height == 0)
			return;

		if (m_Width != width || m_Height != height)
		{
			m_Width = width;
			m_Height = height;
			RecreateSwapChainResources(width, height);
		}

		m_SceneRenderer->SetViewportSize(width, height);
		m_RuntimeScene->OnViewportResize(width, height);
		m_Renderer2DProjection = glm::ortho(0.0f, (float)width, 0.0f, (float)height);

		m_RuntimeScene->OnUpdateRuntime(ts);
		UpdateBenchmarkCamera();
		m_RuntimeScene->OnRenderRuntime(m_SceneRenderer);

		DrawRuntimeOverlay();
		SubmitFinalImageToSwapChain();
		UpdateBenchmark(ts);
	}

	void RuntimeLayer::SubmitFinalImageToSwapChain()
	{
		if (!m_CommandBuffer || !m_SwapChainRenderPass || !m_SwapChainMaterial)
			return;

		if (Ref<Image2D> finalImage = m_SceneRenderer->GetFinalPassImage())
			m_SwapChainMaterial->Set("u_Texture", finalImage);

		m_CommandBuffer->Begin();
		Renderer::BeginRenderPass(m_CommandBuffer, m_SwapChainRenderPass, true);
		if (m_SceneRenderer->GetFinalPassImage())
			Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SwapChainRenderPass->GetPipeline(), m_SwapChainMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		m_CommandBuffer->End();

		SubmitCommandBufferToSwapChain();
	}

	void RuntimeLayer::SubmitCommandBufferToSwapChain()
	{
		Ref<RenderCommandBuffer> commandBuffer = m_CommandBuffer;
		VulkanSwapChain* swapChain = &Application::Get().GetWindow().GetSwapChain();

		Renderer::Submit([commandBuffer, swapChain]() mutable
		{
			vk::Semaphore acquiredSemaphore = swapChain->GetAcquiredImageSemaphore();
			commandBuffer->RT_Submit((VkSemaphore)acquiredSemaphore);
		});
	}

	void RuntimeLayer::DrawRuntimeOverlay()
	{
		if (m_BenchmarkConfig.Enabled)
			return;

		if (!m_Renderer2D || (!m_ShowDebugDisplay && !ShouldShowVersionInfo()))
			return;

		m_Renderer2D->SetTargetFramebuffer(m_SceneRenderer->GetExternalCompositeFramebuffer());
		m_Renderer2D->BeginScene(m_Renderer2DProjection, glm::mat4(1.0f));

		if (m_ShowDebugDisplay)
			DrawDebugStats();
		if (ShouldShowVersionInfo())
			DrawVersionInfo();

		m_Renderer2D->EndScene();
	}

	void RuntimeLayer::DrawDebugStats()
	{
		const float fontSize = 22.0f;
		glm::vec2 position = { 20.0f, (float)m_Height - 35.0f };

		DrawString(std::format("{} fps", m_FramesPerSecond), position, { 0.2f, 1.0f, 0.2f, 1.0f }, fontSize);
		position.y -= fontSize;
		DrawString(std::format("{:.2f} ms frame", m_FrameTime), position, { 0.2f, 1.0f, 0.2f, 1.0f }, fontSize);
		position.y -= fontSize;
		DrawString(std::format("{:.2f} ms CPU", m_CPUTime), position, { 0.2f, 1.0f, 0.2f, 1.0f }, fontSize);
		position.y -= fontSize;
		DrawString(std::format("{:.2f} ms GPU", m_GPUTime), position, { 0.2f, 1.0f, 0.2f, 1.0f }, fontSize);

		const auto gpuStats = Renderer::GetGPUMemoryStats();
		position.y -= fontSize;
		DrawString(std::format("{}/{} VRAM", Utils::BytesToString(gpuStats.Used), Utils::BytesToString(gpuStats.TotalAvailable)), position, { 0.2f, 1.0f, 0.2f, 1.0f }, fontSize);
		position.y -= fontSize;
		DrawString(std::format("{} draws, {} instances", Renderer::GetDrawcallCount(), Renderer::GetInstanceCount()), position, { 0.2f, 1.0f, 0.2f, 1.0f }, fontSize);
		position.y -= fontSize;
		DrawString(std::format("VSync {}", Application::Get().GetWindow().IsVSync() ? "on" : "off"), position, { 0.2f, 1.0f, 0.2f, 1.0f }, fontSize);
	}

	void RuntimeLayer::DrawVersionInfo()
	{
		const float introAlpha = glm::clamp(1.0f - (Application::Get().GetTime() / m_IntroVersionDuration), 0.0f, 1.0f);
		const float alpha = m_ShowVersionInfo ? 1.0f : introAlpha;
		DrawString(LUX_VERSION_LONG, { 20.0f, 25.0f }, { 1.0f, 1.0f, 1.0f, alpha }, 26.0f, false);
		if (m_AssetPack)
			DrawString(std::format("AssetPack {}", m_AssetPack->GetBuildVersion()), { 20.0f, 55.0f }, { 1.0f, 1.0f, 1.0f, alpha }, 20.0f, false);
	}

	void RuntimeLayer::DrawString(const std::string& text, const glm::vec2& position, const glm::vec4& color, float size, bool shadow)
	{
		if (!m_Renderer2D)
			return;

		glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(size));
		if (shadow)
		{
			glm::mat4 shadowTransform = glm::translate(glm::mat4(1.0f), { position.x + 1.0f, position.y - 1.0f, -0.20f }) * scale;
			m_Renderer2D->DrawString(text, Font::GetDefaultMonoSpacedFont(), shadowTransform, 1000.0f, { 0.0f, 0.0f, 0.0f, color.a });
		}

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), { position.x, position.y, -0.21f }) * scale;
		m_Renderer2D->DrawString(text, Font::GetDefaultMonoSpacedFont(), transform, 1000.0f, color);
	}

	void RuntimeLayer::UpdateFPSStat()
	{
		const float frameTime = (float)Application::Get().GetFrametime();
		m_FramesPerSecond = frameTime > 0.0f ? (uint32_t)(1.0f / frameTime) : 0;
	}

	void RuntimeLayer::UpdatePerformanceTimers()
	{
		const auto& app = Application::Get();
		m_FrameTime = (float)app.GetFrametime().GetMilliseconds();

		const auto& appTimers = app.GetPerformanceTimers();
		m_CPUTime = appTimers.MainThreadWorkTime + appTimers.MainThreadWaitTime + appTimers.RenderThreadWorkTime + appTimers.RenderThreadWaitTime;
		m_GPUTime = m_SceneRenderer ? m_SceneRenderer->GetStatistics().TotalGPUTime : 0.0f;
	}

	bool RuntimeLayer::ShouldShowIntroVersion() const
	{
		return Application::Get().GetTime() < m_IntroVersionDuration;
	}

	bool RuntimeLayer::ShouldShowVersionInfo() const
	{
		return m_ShowVersionInfo || ShouldShowIntroVersion();
	}

	void RuntimeLayer::ApplyBenchmarkRendererSettings()
	{
		if (!m_BenchmarkConfig.Enabled || !m_SceneRenderer)
			return;

		m_SceneRenderer->ApplyQualityPreset(QualityPreset::Medium);

		SceneRendererOptions& options = m_SceneRenderer->GetOptions();
		options.ShowGrid = false;
		options.ShowSelectedInWireframe = false;
		options.ShowPhysicsColliders = false;
		options.EnableJumpFlood = false;
		options.EnableTAA = false;
		options.EnableSSRTemporalAccumulation = false;
		options.EnableGTAOTemporalAccumulation = false;

		BloomSettings& bloomSettings = m_SceneRenderer->GetBloomSettings();
		bloomSettings.Enabled = true;
		bloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;

		DOFSettings& dofSettings = m_SceneRenderer->GetDOFSettings();
		dofSettings.Enabled = false;

		m_BenchmarkSamples.reserve(m_BenchmarkConfig.CaptureFrames);
		LUX_CORE_INFO("Runtime benchmark '{}' enabled: warmup={} frames, capture={} frames, output='{}'.",
			m_BenchmarkConfig.Name,
			m_BenchmarkConfig.WarmupFrames,
			m_BenchmarkConfig.CaptureFrames,
			m_BenchmarkConfig.OutputPath.string());
	}

	void RuntimeLayer::UpdateBenchmarkCamera()
	{
		if (!m_BenchmarkConfig.Enabled || !m_RuntimeScene)
			return;

		Entity cameraEntity = m_RuntimeScene->GetPrimaryCameraEntity();
		if (!cameraEntity || !cameraEntity.HasComponent<TransformComponent>())
			return;

		const uint32_t totalFrames = std::max(1u, m_BenchmarkConfig.WarmupFrames + m_BenchmarkConfig.CaptureFrames);
		const float normalizedFrame = (float)(m_BenchmarkFrameIndex % totalFrames) / (float)totalFrames;
		const float angle = normalizedFrame * glm::two_pi<float>();

		const glm::vec3 target = { 0.0f, 2.0f, 0.0f };
		const glm::vec3 position = {
			std::sin(angle) * 8.0f,
			3.0f + std::sin(angle * 0.5f) * 0.5f,
			std::cos(angle) * 8.0f
		};

		TransformComponent& transform = cameraEntity.GetComponent<TransformComponent>();
		transform.SetTransform(glm::inverse(glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f))));
	}

	void RuntimeLayer::UpdateBenchmark(Timestep ts)
	{
		(void)ts;

		if (!m_BenchmarkConfig.Enabled || m_BenchmarkComplete || !m_SceneRenderer)
			return;

		UpdatePerformanceTimers();
		m_BenchmarkFrameIndex++;

		if (m_BenchmarkFrameIndex <= m_BenchmarkConfig.WarmupFrames)
			return;

		if (m_BenchmarkConfig.CaptureFrames == 0)
		{
			FinishBenchmark();
			return;
		}

		const uint32_t capturedFrame = m_BenchmarkFrameIndex - m_BenchmarkConfig.WarmupFrames;
		const SceneRenderer::Statistics& stats = m_SceneRenderer->GetStatistics();
		const SceneRendererOptions& options = m_SceneRenderer->GetOptions();
		const BloomSettings& bloomSettings = m_SceneRenderer->GetBloomSettings();
		const DOFSettings& dofSettings = m_SceneRenderer->GetDOFSettings();

		BenchmarkFrameSample& sample = m_BenchmarkSamples.emplace_back();
		sample.Frame = capturedFrame;
		sample.FrameTimeMS = m_FrameTime;
		sample.CPUTimeMS = m_CPUTime;
		sample.GPUTimeMS = m_GPUTime;
		sample.RenderGraphPassCount = stats.MemoryStats.RenderGraphPassCount;
		sample.DrawCalls = stats.DrawCalls;
		sample.IndirectDraws = stats.IndirectDraws;
		sample.VisibleInstances = stats.VisibleInstances;
		sample.RenderScale = ResolveBenchmarkRenderScale(options);
		sample.SSR = options.EnableSSR;
		sample.GTAO = options.EnableGTAO;
		sample.Bloom = bloomSettings.Enabled;
		sample.TAA = options.EnableTAA;
		sample.DOF = dofSettings.Enabled;
		sample.JumpFlood = options.EnableJumpFlood;

		if (m_BenchmarkSamples.size() >= m_BenchmarkConfig.CaptureFrames)
			FinishBenchmark();
	}

	void RuntimeLayer::FinishBenchmark()
	{
		if (m_BenchmarkComplete)
			return;

		m_BenchmarkComplete = true;
		WriteBenchmarkResults();
		Application::Get().Close();
	}

	void RuntimeLayer::WriteBenchmarkResults() const
	{
		const std::filesystem::path csvPath = ResolveBenchmarkCSVPath(m_BenchmarkConfig.OutputPath);
		const std::filesystem::path jsonPath = ResolveBenchmarkJSONPath(m_BenchmarkConfig.OutputPath);
		EnsureParentDirectory(csvPath);
		EnsureParentDirectory(jsonPath);

		std::vector<float> frameTimes;
		std::vector<float> cpuTimes;
		std::vector<float> gpuTimes;
		frameTimes.reserve(m_BenchmarkSamples.size());
		cpuTimes.reserve(m_BenchmarkSamples.size());
		gpuTimes.reserve(m_BenchmarkSamples.size());

		for (const BenchmarkFrameSample& sample : m_BenchmarkSamples)
		{
			frameTimes.push_back(sample.FrameTimeMS);
			cpuTimes.push_back(sample.CPUTimeMS);
			gpuTimes.push_back(sample.GPUTimeMS);
		}

		const BenchmarkTimingSummary frameSummary = SummarizeTiming(std::move(frameTimes));
		const BenchmarkTimingSummary cpuSummary = SummarizeTiming(std::move(cpuTimes));
		const BenchmarkTimingSummary gpuSummary = SummarizeTiming(std::move(gpuTimes));

		{
			std::ofstream csv(csvPath);
			if (!csv)
			{
				LUX_CORE_ERROR("Could not open benchmark CSV output '{}'.", csvPath.string());
			}
			else
			{
				csv << "frame,frame_ms,cpu_ms,gpu_ms,render_graph_passes,draw_calls,indirect_draws,visible_instances,render_scale,ssr,gtao,bloom,taa,dof,jump_flood\n";
				csv << std::fixed << std::setprecision(4);
				for (const BenchmarkFrameSample& sample : m_BenchmarkSamples)
				{
					csv
						<< sample.Frame << ','
						<< sample.FrameTimeMS << ','
						<< sample.CPUTimeMS << ','
						<< sample.GPUTimeMS << ','
						<< sample.RenderGraphPassCount << ','
						<< sample.DrawCalls << ','
						<< sample.IndirectDraws << ','
						<< sample.VisibleInstances << ','
						<< sample.RenderScale << ','
						<< (sample.SSR ? 1 : 0) << ','
						<< (sample.GTAO ? 1 : 0) << ','
						<< (sample.Bloom ? 1 : 0) << ','
						<< (sample.TAA ? 1 : 0) << ','
						<< (sample.DOF ? 1 : 0) << ','
						<< (sample.JumpFlood ? 1 : 0) << '\n';
				}
			}
		}

		{
			std::ofstream json(jsonPath);
			if (!json)
			{
				LUX_CORE_ERROR("Could not open benchmark JSON output '{}'.", jsonPath.string());
			}
			else
			{
				json << std::fixed << std::setprecision(4);
				json << "{\n";
				json << "  \"benchmark\": \"" << m_BenchmarkConfig.Name << "\",\n";
				json << "  \"warmup_frames\": " << m_BenchmarkConfig.WarmupFrames << ",\n";
				json << "  \"captured_frames\": " << m_BenchmarkSamples.size() << ",\n";
				json << "  \"summary\": {\n";
				json << "    \"frame_ms\": { \"average\": " << frameSummary.Average << ", \"p95\": " << frameSummary.P95 << ", \"p99\": " << frameSummary.P99 << " },\n";
				json << "    \"cpu_ms\": { \"average\": " << cpuSummary.Average << ", \"p95\": " << cpuSummary.P95 << ", \"p99\": " << cpuSummary.P99 << " },\n";
				json << "    \"gpu_ms\": { \"average\": " << gpuSummary.Average << ", \"p95\": " << gpuSummary.P95 << ", \"p99\": " << gpuSummary.P99 << " }\n";
				json << "  },\n";
				json << "  \"samples\": [\n";
				for (size_t index = 0; index < m_BenchmarkSamples.size(); index++)
				{
					const BenchmarkFrameSample& sample = m_BenchmarkSamples[index];
					json << "    { \"frame\": " << sample.Frame
						<< ", \"frame_ms\": " << sample.FrameTimeMS
						<< ", \"cpu_ms\": " << sample.CPUTimeMS
						<< ", \"gpu_ms\": " << sample.GPUTimeMS
						<< ", \"render_graph_passes\": " << sample.RenderGraphPassCount
						<< ", \"draw_calls\": " << sample.DrawCalls
						<< ", \"indirect_draws\": " << sample.IndirectDraws
						<< ", \"visible_instances\": " << sample.VisibleInstances
						<< ", \"render_scale\": " << sample.RenderScale
						<< ", \"active_effects\": { \"ssr\": " << (sample.SSR ? "true" : "false")
						<< ", \"gtao\": " << (sample.GTAO ? "true" : "false")
						<< ", \"bloom\": " << (sample.Bloom ? "true" : "false")
						<< ", \"taa\": " << (sample.TAA ? "true" : "false")
						<< ", \"dof\": " << (sample.DOF ? "true" : "false")
						<< ", \"jump_flood\": " << (sample.JumpFlood ? "true" : "false") << " } }";
					json << (index + 1 < m_BenchmarkSamples.size() ? ",\n" : "\n");
				}
				json << "  ]\n";
				json << "}\n";
			}
		}

		LUX_CORE_INFO("Benchmark '{}' wrote {} samples to '{}' and '{}'. Average frame {:.2f} ms, p95 {:.2f} ms, p99 {:.2f} ms.",
			m_BenchmarkConfig.Name,
			m_BenchmarkSamples.size(),
			csvPath.string(),
			jsonPath.string(),
			frameSummary.Average,
			frameSummary.P95,
			frameSummary.P99);
	}

	void RuntimeLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& keyEvent)
		{
			if (keyEvent.GetRepeatCount() == 0 && Input::IsKeyDown(KeyCode::LeftControl) && keyEvent.GetKeyCode() == KeyCode::F3)
			{
				m_ShowDebugDisplay = !m_ShowDebugDisplay;
				return true;
			}

			if (keyEvent.GetRepeatCount() == 0 && keyEvent.GetKeyCode() == KeyCode::F1)
			{
				m_ShowVersionInfo = !m_ShowVersionInfo;
				return true;
			}

			if (keyEvent.GetRepeatCount() == 0 && keyEvent.GetKeyCode() == KeyCode::F2)
			{
				Window& window = Application::Get().GetWindow();
				const bool enableVSync = !window.IsVSync();
				window.SetVSync(enableVSync);
				LUX_CORE_INFO("Runtime VSync {}", enableVSync ? "enabled" : "disabled");
				return true;
			}

			return false;
		});
	}
}
