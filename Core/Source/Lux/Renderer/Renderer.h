

#pragma once

#include "RendererContext.h"
#include "RenderCommandQueue.h"
#include "RenderCommandBuffer.h"
#include "Pipeline.h"
#include "PipelineCompute.h"
#include "Mesh.h"
#include "UniformBufferSet.h"
#include "StorageBufferSet.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/RenderThread.h"

#include "RendererCapabilities.h"
#include "RendererConfig.h"

#include "GPUStats.h"
#include "Material.h"
#include "SceneEnvironment.h"
#include "Texture.h"

#include "nvrhi/nvrhi.h"
#include "Lux/Scene/Scene.h"

namespace Lux {

	class ShaderLibrary;
	class RenderPass;
	class ComputePass;

	// Unified draw command for mesh rendering - consolidates parameters for all mesh draw variants
	struct MeshDrawCommand
	{
		Ref<MeshSource> MeshSource;

		// For MaterialTable path. Can be null for a mesh draw with material override
		Ref<MaterialTable> MaterialTable;

		uint32_t SubmeshIndex = 0;
		uint32_t ObjectIndexBase = 0;
		uint32_t InstanceCount = 1;

		//for rigged meshes
		uint32_t BoneTransformsOffset = 0;
		uint32_t BoneTransformsStride = 0;

		bool IsRigged = false;
	};
	
	class Renderer
	{
	public:
		typedef void(*RenderCommandFn)(void*);

		static Ref<RendererContext> GetContext()
		{
			return Application::Get().GetWindow().GetRenderContext();
		}

		static void Init();
		static void Shutdown();

		static RendererCapabilities& GetCapabilities() { static RendererCapabilities caps; return caps; }

		static Ref<ShaderLibrary> GetShaderLibrary();

		template<typename FuncT>
		static void Submit(FuncT&& func)
		{
			auto renderCmd = [](void* ptr) {
				auto pFunc = (FuncT*)ptr;
				(*pFunc)();

				// NOTE: Instead of destroying we could try and enforce all items to be trivally destructible
				// however some items like uniforms which contain std::strings still exist for now
				// static_assert(std::is_trivially_destructible_v<FuncT>, "FuncT must be trivially destructible");
				pFunc->~FuncT();
				};
			auto storageBuffer = GetRenderCommandQueue().Allocate(renderCmd, sizeof(func));
			new (storageBuffer) FuncT(std::forward<FuncT>(func));
		}

		template<typename FuncT>
		static void SubmitResourceFree(FuncT&& func)
		{
			auto renderCmd = [](void* ptr) {
				auto pFunc = (FuncT*)ptr;
				(*pFunc)();

				// NOTE: Instead of destroying we could try and enforce all items to be trivally destructible
				// however some items like uniforms which contain std::strings still exist for now
				// static_assert(std::is_trivially_destructible_v<FuncT>, "FuncT must be trivially destructible");
				pFunc->~FuncT();
				};

			if (RenderThread::IsCurrentThreadRT())
			{
				const uint32_t index = Renderer::RT_GetCurrentFrameIndex();
				auto storageBuffer = GetRenderResourceReleaseQueue(index).Allocate(renderCmd, sizeof(func));
				new (storageBuffer) FuncT(std::forward<FuncT>((FuncT&&)func));
			}
			else
			{
				const uint32_t index = Renderer::GetCurrentFrameIndex();
				Submit([renderCmd, func, index]()
					{
						auto storageBuffer = GetRenderResourceReleaseQueue(index).Allocate(renderCmd, sizeof(func));
						new (storageBuffer) FuncT(std::forward<FuncT>((FuncT&&)func));
					});
			}
		}

		/*static void* Submit(RenderCommandFn fn, unsigned int size)
		{
			return s_Instance->m_CommandQueue.Allocate(fn, size);
		}*/

		static void WaitAndRender(RenderThread* renderThread);
		static void SwapQueues();

		static void RenderThreadFunc(RenderThread* renderThread);
		static uint32_t GetRenderQueueIndex();
		static uint32_t GetRenderQueueSubmissionIndex();

		// ~Actual~ Renderer here... TODO: remove confusion later

		// Render Pass API
		static void BeginRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, bool explicitClear = false);
		static void EndRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer);

		// Dynamic viewport/scissor control
		static void SetViewport(Ref<RenderCommandBuffer> renderCommandBuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
		static void SetScissor(Ref<RenderCommandBuffer> renderCommandBuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

		// Compute Pass API
		static void BeginComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass);
		static void EndComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass);
		static void DispatchCompute(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3& workGroups, Buffer constants = Buffer());

		static void BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor = {});
		static void EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer);

		static void RT_BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor = {});
		static void RT_EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer);

		static void BeginFrame();
		static void EndFrame();
		
		//static void SetSceneEnvironment(Ref<SceneRenderer> sceneRenderer, Ref<Environment> environment, Ref<Image2D> shadow, Ref<Image2D> spotShadow);
		static std::pair<Ref<TextureCube>, Ref<TextureCube>> CreateEnvironmentMap(const std::string& filepath);
		static Ref<TextureCube> CreatePreethamSky(float turbidity, float azimuth, float inclination);
		static Ref<Environment> CreatePreethamSkyEnvironment(float turbidity, float azimuth, float inclination);
		
		// Renders a mesh using material from MaterialTable lookup
		// Use for geometry passes where materials come from the mesh's material table
		static void RenderMesh(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, const MeshDrawCommand& drawCmd);

		// Renders a mesh with an explicit material override
		// Use for shadow passes, pre-depth, selection, wireframe, etc.
		static void RenderMesh(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, const MeshDrawCommand& drawCmd, Ref<Material> material, int32_t lightIndex = -1);

		static void RenderQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, const glm::mat4& transform);
		static void SubmitFullscreenQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material);
		static void SubmitFullscreenQuadWithOverrides(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Buffer vertexShaderOverrides, Buffer fragmentShaderOverrides);
		static void LightCulling(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3& workGroups);
		static void RenderGeometry(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Ref<VertexBuffer> vertexBuffer, Ref<IndexBuffer> indexBuffer, const glm::mat4& transform, uint32_t indexCount = 0);
		static void ClearImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, nvrhi::Color clearColor, nvrhi::TextureSubresourceSet subresourceSet = nvrhi::TextureSubresourceSet());
		static void CopyImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage);
		static void BlitImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage);

		static Ref<Texture2D> GetWhiteTexture();
		static Ref<Texture2D> GetBlackTexture();
		static Ref<Texture2D> GetHilbertLut();
		static Ref<Texture2D> GetBRDFLutTexture();
		static Ref<TextureCube> GetBlackCubeTexture();
		static Ref<Material> GetDefaultWhiteMaterial();
		static Ref<Environment> GetEmptyEnvironment();
		static Ref<Environment> GetDefaultEnvironment();

		static void RegisterShaderDependency(Ref<Shader> shader, PipelineCompute* computePipeline);
		static void RegisterShaderDependency(Ref<Shader> shader, Pipeline* pipeline);
		static void RegisterShaderDependency(Ref<Shader> shader, Material* material);
		static void OnShaderReloaded(size_t hash);

		static uint32_t GetCurrentFrameIndex();
		static uint32_t RT_GetCurrentFrameIndex();

		static RendererConfig& GetConfig();
		static void SetConfig(const RendererConfig& config);

		static RenderCommandQueue& GetRenderResourceReleaseQueue(uint32_t index);

		// Add known macro from shader.
		static const std::unordered_map<std::string, std::string>& GetGlobalShaderMacros();
		static void AcknowledgeParsedGlobalMacros(const std::unordered_set<std::string>& macros, Ref<Shader> shader);
		static void SetMacroInShader(Ref<Shader> shader, const std::string& name, const std::string& value = "");
		static void SetGlobalMacroInShaders(const std::string& name, const std::string& value = "");
		static uint32_t ReloadShaders(bool forceCompile = true);
		static uint32_t WarmUpShaderPipelines();
		static uint32_t GetShaderPermutationCacheSize();
		// Returns true if any shader is actually updated.
		static bool UpdateDirtyShaders();

		static GPUMemoryStats GetGPUMemoryStats();

		static Ref<Sampler> GetClampSampler();
		static Ref<Sampler> GetPointSampler();
		static Ref<Sampler> GetRepeatSampler();
		static Ref<Sampler> GetDefaultSampler() { return GetClampSampler(); }

		static int GetDrawcallCount();
		static int GetInstanceCount();
	private:
		static RenderCommandQueue& GetRenderCommandQueue();

		// Internal helper for binding mesh vertex/index buffers (render thread only)
		//static void RT_BindMeshBuffers(nvrhi::GraphicsState& graphicsState, Ref<MeshSource> meshSource, bool bindBoneInfluences);
	};

	namespace Utils {

		inline void DumpGPUInfo()
		{
			auto& caps = Renderer::GetCapabilities();
			LUX_CORE_TRACE_TAG("Renderer", "GPU Info:");
			LUX_CORE_TRACE_TAG("Renderer", "  Vendor: {0}", caps.Vendor);
			LUX_CORE_TRACE_TAG("Renderer", "  Device: {0}", caps.Device);
			LUX_CORE_TRACE_TAG("Renderer", "  Version: {0}", caps.Version);
		}

	}

}
