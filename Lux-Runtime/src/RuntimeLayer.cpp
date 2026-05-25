#include "RuntimeLayer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Application.h"
#include "Lux/Core/Input.h"
#include "Lux/Project/Project.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Utilities/StringUtils.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include <format>

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
	}

	RuntimeLayer::RuntimeLayer(std::filesystem::path projectPath)
		: m_ProjectPath(std::move(projectPath))
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

		ScriptEngine::Init();

		return LoadScene(m_RuntimeProject->GetConfig().StartSceneHandle);
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

		Application::Get().GetWindow().SetTitle(m_RuntimeScene->GetName().empty() ? "Lux Runtime" : m_RuntimeScene->GetName());
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
		m_CommandBuffer = RenderCommandBuffer::Create(0, "RuntimeSwapChain", true);
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
		m_RuntimeScene->OnRenderRuntime(m_SceneRenderer);

		DrawRuntimeOverlay();
		SubmitFinalImageToSwapChain();
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
		if (!m_Renderer2D || (!m_ShowDebugDisplay && !ShouldShowIntroVersion()))
			return;

		m_Renderer2D->SetTargetFramebuffer(m_SceneRenderer->GetExternalCompositeFramebuffer());
		m_Renderer2D->BeginScene(m_Renderer2DProjection, glm::mat4(1.0f));

		if (m_ShowDebugDisplay)
			DrawDebugStats();
		if (ShouldShowIntroVersion())
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
	}

	void RuntimeLayer::DrawVersionInfo()
	{
		const float alpha = glm::clamp(1.0f - (Application::Get().GetTime() / m_IntroVersionDuration), 0.0f, 1.0f);
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

			return false;
		});
	}
}
