#include "lpch.h"
#include "SceneRendererInternal.h"

namespace Lux {

	SceneRenderer::RendererFrameDebugSnapshot SceneRenderer::GetRendererFrameDebugSnapshot() const
	{
		const ResolvedFrameEnvironment frame = ResolveFrameEnvironment();

		RendererFrameDebugSnapshot snapshot;
		snapshot.DeferredPath = frame.DeferredPath;
		snapshot.HasRenderScene = m_SubmittedRenderScene != nullptr;
		snapshot.HasRenderVolumeEnvironment = frame.HasRenderVolumeEnvironment;
		snapshot.SkyAtmosphereEnabled = frame.SkyAtmosphereEnabled;
		snapshot.VolumetricCloudsEnabled = frame.VolumetricCloudsEnabled;
		snapshot.HeightFogEnabled = frame.HeightFogEnabled;
		snapshot.LocalFogEnabled = frame.LocalFogEnabled;
		snapshot.BloomEnabled = frame.BloomEnabled;
		snapshot.DOFEnabled = frame.DOFEnabled;
		snapshot.ActiveVolumeCount = frame.Volumes.ActiveVolumeCount;
		snapshot.ActivePostProcessVolumeCount = frame.Volumes.ActivePostProcessVolumeCount;
		snapshot.ActiveAtmosphereVolumeCount = frame.Volumes.ActiveAtmosphereVolumeCount;
		snapshot.LocalFogVolumeCount = frame.Volumes.LocalFogVolumeCount;
		snapshot.CulledLocalFogVolumeCount = frame.Volumes.CulledLocalFogVolumeCount;
		snapshot.DroppedLocalFogVolumeCount = frame.Volumes.DroppedLocalFogVolumeCount;
		return snapshot;
	}

	void SceneRenderer::UpdateMemoryStatistics()
	{
		// Memory statistics are display-only (Render Stats / Renderer Debugger panels) and
		// are gathered with a full VMA allocation walk (vmaCalculateStats) plus a render-graph
		// alias-plan pass — far too heavy to run every frame for numbers that change slowly.
		// Refresh a few times per second and keep the previous values in between.
		if (m_MemoryStatsCountdown > 0)
		{
			m_MemoryStatsCountdown--;
			return;
		}
		m_MemoryStatsCountdown = MemoryStatsRefreshFrameInterval;

		auto& memoryStats = m_Statistics.MemoryStats;
		memoryStats = {};

		const GPUMemoryStats gpuStats = Renderer::GetGPUMemoryStats();
		memoryStats.BudgetBytes = gpuStats.TotalAvailable;
		memoryStats.UsedBytes = gpuStats.Used;
		memoryStats.BufferBytes = gpuStats.BufferAllocationSize;
		memoryStats.BufferCount = static_cast<uint32_t>(gpuStats.BufferAllocationCount);

		uint64_t estimatedBufferBytes = 0;
		uint32_t estimatedBufferCount = 0;
		auto addUniformBufferSet = [&](const Ref<UniformBufferSet>& bufferSet)
			{
				if (!bufferSet)
					return;

				estimatedBufferBytes += bufferSet->GetAllocatedSize();
				estimatedBufferCount += bufferSet->GetBufferCount();
			};
		auto addStorageBufferSet = [&](const Ref<StorageBufferSet>& bufferSet)
			{
				if (!bufferSet)
					return;

				estimatedBufferBytes += bufferSet->GetAllocatedSize();
				estimatedBufferCount += bufferSet->GetBufferCount();
			};

		addUniformBufferSet(m_UBSCamera);
		addUniformBufferSet(m_UBSScene);
		addUniformBufferSet(m_UBSShadow);
		addUniformBufferSet(m_UBSSpotShadow);
		addUniformBufferSet(m_UBSRendererData);
		addUniformBufferSet(m_UBSScreenData);
		addUniformBufferSet(m_UBSAtmosphere);
		addUniformBufferSet(m_UBSPointLights);
		addUniformBufferSet(m_UBSSpotLights);
		addStorageBufferSet(m_SBSObjectIndexes);
		addStorageBufferSet(m_SBSVisibleObjectIndexes);
		addStorageBufferSet(m_SBSGPUSceneInstances);
		addStorageBufferSet(m_SBSGPUMaterials);
		addStorageBufferSet(m_SBSMeshCullDrawData);
		addStorageBufferSet(m_SBSIndirectDrawCommands);
		addStorageBufferSet(m_SBSClusterAABBs);
		addStorageBufferSet(m_SBSPointLightGrid);
		addStorageBufferSet(m_SBSSpotLightGrid);
		addStorageBufferSet(m_SBSPointLightIndexList);
		addStorageBufferSet(m_SBSSpotLightIndexList);
		addStorageBufferSet(m_SBSClusterLightCounter);

		memoryStats.BufferBytes = std::max(memoryStats.BufferBytes, estimatedBufferBytes);
		memoryStats.BufferCount = std::max(memoryStats.BufferCount, estimatedBufferCount);

		std::unordered_set<uint64_t> renderTargetImageHandles;
		std::unordered_set<const Framebuffer*> framebuffers;

		auto addRenderTargetImage = [&](const Ref<Image2D>& image)
			{
				if (!image || image->GetHandle() == nullptr)
					return;

				const uint64_t handle = reinterpret_cast<uint64_t>(image->GetHandle().Get());
				if (!renderTargetImageHandles.insert(handle).second)
					return;

				memoryStats.RenderTargetCount++;
				uint64_t imageSize = image->GetGPUMemoryUsage();
				if (imageSize == 0)
				{
					const ImageSpecification& spec = image->GetSpecification();
					imageSize = Utils::GetImageMemorySize(spec.Format, spec.Width, spec.Height, spec.Mips, spec.Layers);
				}
				memoryStats.RenderTargetBytes += imageSize;
			};

		auto addFramebuffer = [&](const Ref<Framebuffer>& framebuffer)
			{
				if (!framebuffer || !framebuffers.insert(framebuffer.Raw()).second)
					return;

				memoryStats.FramebufferCount++;
				for (uint32_t attachment = 0; attachment < framebuffer->GetColorAttachmentCount(); attachment++)
					addRenderTargetImage(framebuffer->GetImage(attachment));

				if (framebuffer->HasDepthAttachment())
					addRenderTargetImage(framebuffer->GetDepthImage());
			};

		auto addRenderPass = [&](const Ref<RenderPass>& pass)
			{
				if (!pass)
					return;

				addFramebuffer(pass->GetTargetFramebuffer());

				memoryStats.DescriptorSetCount += pass->GetBindingSetCount();
			};

		auto addComputePass = [&](const Ref<ComputePass>& pass)
			{
				if (!pass)
					return;

				memoryStats.DescriptorSetCount += pass->GetBindingSetCount();
			};

		for (const Ref<RenderPass>& pass : m_ShadowMapPasses)
			addRenderPass(pass);
		addRenderPass(m_SpotShadowMapPass);
		addRenderPass(m_PreDepthPass);
		addRenderPass(m_AOCompositePass);
		addRenderPass(m_AODebugPass);
		addRenderPass(m_SSRCompositePass);
		addRenderPass(m_DOFPass);
		addRenderPass(m_JumpFloodInitPass);
		addRenderPass(m_JumpFloodPasses[0]);
		addRenderPass(m_JumpFloodPasses[1]);
		addRenderPass(m_JumpFloodCompositePass);
		addRenderPass(m_GeometryPass);
		addRenderPass(m_GeometryPassTransparent);
		addRenderPass(m_DeferredLightingPass);
		addRenderPass(m_GBufferDebugPass);
		addRenderPass(m_SelectedGeometryPass);
		addRenderPass(m_GeometryWireframePass);
		addRenderPass(m_SkyboxPass);
		addRenderPass(m_SkyAtmospherePass);
		addRenderPass(m_VolumetricCloudPass);
		addRenderPass(m_VolumetricCloudCompositePass);
		addRenderPass(m_AtmosphericFogPass);
		addRenderPass(m_CompositePass);
		addRenderPass(m_GridRenderPass);

		addFramebuffer(m_GeometryPassFramebuffer);
		addFramebuffer(m_SceneColorFramebuffer);
		addFramebuffer(m_CompositingFramebuffer);

		addComputePass(m_MeshCullingPass);
		addComputePass(m_ClusterBuildPass);
		addComputePass(m_ClusterLightCullingPass);
		addComputePass(m_HierarchicalDepthPass);
		addComputePass(m_PreIntegrationPass);
		addComputePass(m_PreConvolutionComputePass);
		addComputePass(m_GTAOComputePass);
		addComputePass(m_GTAODenoisePass[0]);
		addComputePass(m_GTAODenoisePass[1]);
		addComputePass(m_GTAOTemporalPass);
		addComputePass(m_SSRPass);
		addComputePass(m_SSRTemporalPass);
		addComputePass(m_BloomComputePass);

		if (m_HierarchicalDepthTexture.Texture)
			addRenderTargetImage(m_HierarchicalDepthTexture.Texture->GetImage());
		if (m_PreIntegrationVisibilityTexture.Texture)
			addRenderTargetImage(m_PreIntegrationVisibilityTexture.Texture->GetImage());
		if (m_PreConvolutedTexture.Texture)
			addRenderTargetImage(m_PreConvolutedTexture.Texture->GetImage());
		for (const BloomComputeTextures& bloomTexture : m_BloomComputeTextures)
		{
			if (bloomTexture.Texture)
				addRenderTargetImage(bloomTexture.Texture->GetImage());
		}

		addRenderTargetImage(m_GTAOOutputImage);
		addRenderTargetImage(m_GTAODenoiseImage);
		addRenderTargetImage(m_GTAOFinalImage);
		for (const Ref<Image2D>& historyImage : m_GTAOHistoryImages)
			addRenderTargetImage(historyImage);
		addRenderTargetImage(m_GTAOEdgesOutputImage);
		addRenderTargetImage(m_SSRImage);
		addRenderTargetImage(m_SSRFinalImage);
		for (const Ref<Image2D>& historyImage : m_SSRHistoryImages)
			addRenderTargetImage(historyImage);

		uint64_t liveImageBytes = 0;
		uint32_t liveImageCount = 0;
		for (const auto& [handle, image] : Image2D::GetImageRefs())
		{
			if (!image)
				continue;

			uint64_t imageSize = image->GetGPUMemoryUsage();
			if (imageSize == 0)
			{
				const ImageSpecification& spec = image->GetSpecification();
				imageSize = Utils::GetImageMemorySize(spec.Format, spec.Width, spec.Height, spec.Mips, spec.Layers);
			}

			liveImageBytes += imageSize;
			liveImageCount++;
		}

		const uint64_t imageAllocationSize = std::max(gpuStats.ImageAllocationSize, liveImageBytes);
		const uint32_t imageAllocationCount = std::max<uint32_t>(static_cast<uint32_t>(gpuStats.ImageAllocationCount), liveImageCount);
		memoryStats.TextureBytes = imageAllocationSize > memoryStats.RenderTargetBytes
			? imageAllocationSize - memoryStats.RenderTargetBytes
			: 0;
		memoryStats.TextureCount = imageAllocationCount > memoryStats.RenderTargetCount
			? imageAllocationCount - memoryStats.RenderTargetCount
			: 0;

		UpdateRenderGraphStatistics();
	}

	SceneRenderer::RenderGraphDebugSnapshot SceneRenderer::GetRenderGraphDebugSnapshot()
	{
		BuildRenderGraph();

		RenderGraphDebugSnapshot snapshot;
		const auto compileResult = m_RenderGraph.Compile();
		const auto& textures = m_RenderGraph.GetTextures();
		const auto& passes = m_RenderGraph.GetPasses();
		snapshot.ErrorCount = compileResult.ErrorCount;
		snapshot.WarningCount = compileResult.WarningCount;
		snapshot.InfoCount = compileResult.InfoCount;
		snapshot.ExecutedPassCount = static_cast<uint32_t>(compileResult.ExecutionOrder.size());
		snapshot.CulledPassCount = static_cast<uint32_t>(compileResult.CulledPasses.size());

		auto findProfile = [&](const std::string& passName) -> const PassProfile*
			{
				auto remapProfileName = [](const std::string& name) -> const char*
					{
						if (name == "Directional Shadow Maps") return "ShadowMapPass";
						if (name == "Spot Shadow Maps") return "SpotShadowMapPass";
						if (name == "PreDepth") return "PreDepthPass";
						if (name == "Mesh Culling") return "MeshCullingPass";
						if (name == "Skybox") return "SkyboxPass";
						if (name == "Sky Atmosphere") return "SkyAtmospherePass";
						if (name == "Volumetric Clouds") return "VolumetricCloudPass";
						if (name == "Volumetric Cloud Temporal") return "VolumetricCloudTemporalPass";
						if (name == "Volumetric Cloud Composite") return "VolumetricCloudCompositePass";
						if (name == "Atmospheric Fog") return "AtmosphericFogPass";
						if (name == "Selected Geometry") return "SelectedGeometryPass";
						if (name == "GBuffer") return "GBufferPass";
						if (name == "Deferred Lighting") return "DeferredLightingPass";
						if (name == "Transparent Forward") return "TransparentForwardPass";
						if (name == "Geometry Wireframe") return "GeometryWireframePass";
						if (name == "GBuffer Debug") return "GBufferDebugPass";
						if (name == "GTAO Denoise") return "GTAO-Denoise";
						if (name == "GTAO Temporal") return "GTAO-Temporal";
						if (name == "AO Composite") return "AOComposite";
						if (name == "AO Debug") return "AODebug";
						if (name == "Pre-Convolution") return "PreConvolution";
						if (name == "SSR Temporal") return "SSR-Temporal";
						if (name == "SSR Composite") return "SSRComposite";
						if (name == "JumpFlood Composite") return "JumpFloodComposite";
						if (name == "Bloom") return "BloomCompute";
						if (name == "Composite") return "CompositePass";
						if (name == "Grid") return "GridPass";
						if (name == "Cluster Build") return "ClusterBuildPass";
						if (name == "Cluster Light Culling") return "ClusterLightCullingPass";
						return name.c_str();
					};

				const char* profileName = remapProfileName(passName);
				for (const PassProfile& profile : m_Statistics.PassProfiles)
				{
					if (profile.Name && std::strcmp(profile.Name, profileName) == 0)
						return &profile;
				}
				return nullptr;
			};

		snapshot.Textures.reserve(textures.size());
		for (uint32_t resource = 0; resource < textures.size(); resource++)
		{
			const RenderGraph::TextureDesc& texture = textures[resource];
			RenderGraphTextureDebugInfo& textureInfo = snapshot.Textures.emplace_back();
			textureInfo.Resource = resource;
			textureInfo.Name = texture.Name;
			textureInfo.Format = texture.Format;
			textureInfo.Usage = texture.Usage;
			textureInfo.Dimension = texture.Dimension;
			textureInfo.Width = texture.Width;
			textureInfo.Height = texture.Height;
			textureInfo.Mips = texture.Mips;
			textureInfo.Layers = texture.Layers;
			textureInfo.EstimatedBytes = Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
			textureInfo.Transient = texture.Transient;
			textureInfo.AllowAlias = texture.AllowAlias;

			if (resource < compileResult.Lifetimes.size())
			{
				const RenderGraph::ResourceLifetime& lifetime = compileResult.Lifetimes[resource];
				textureInfo.FirstPass = lifetime.FirstPass;
				textureInfo.LastPass = lifetime.LastPass;
				textureInfo.AliasGroup = lifetime.AliasIndex;
			}

			if (resource < compileResult.ResourceFirstWriter.size())
				textureInfo.FirstWriter = compileResult.ResourceFirstWriter[resource];
			if (resource < compileResult.ResourceLastReader.size())
				textureInfo.LastReader = compileResult.ResourceLastReader[resource];
			if (resource < compileResult.ResourceConsumers.size())
				textureInfo.Consumers = compileResult.ResourceConsumers[resource];

			if (texture.Image)
			{
				textureInfo.AliasedNow = texture.Image->IsTransientAlias();
				textureInfo.CurrentState = texture.Image->GetImageInfo().State;
			}
		}

		for (const RenderGraph::ResourceLifetime& lifetime : compileResult.Lifetimes)
		{
			if (lifetime.FirstPass == UINT32_MAX || lifetime.Resource >= textures.size())
				continue;

			const RenderGraph::TextureDesc& texture = textures[lifetime.Resource];
			if (!texture.Transient || !texture.AllowAlias)
				continue;

			snapshot.TransientBytes += Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
		}

		snapshot.AliasGroups.reserve(compileResult.AliasGroups.size());
		for (const RenderGraph::AliasGroupSummary& aliasGroup : compileResult.AliasGroups)
		{
			RenderGraphAliasGroupDebugInfo& aliasInfo = snapshot.AliasGroups.emplace_back();
			aliasInfo.AliasGroup = aliasGroup.AliasIndex;
			aliasInfo.Compatible = aliasGroup.Compatible;

			for (RenderGraph::ResourceHandle resource : aliasGroup.Resources)
			{
				if (resource >= textures.size())
					continue;

				aliasInfo.Resources.push_back(resource);
				const RenderGraph::TextureDesc& texture = textures[resource];
				const uint64_t size = Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
				aliasInfo.EstimatedBytes += size;
				aliasInfo.BackingBytes = std::max(aliasInfo.BackingBytes, size);
			}

			aliasInfo.SavedBytes = aliasInfo.EstimatedBytes > aliasInfo.BackingBytes ? aliasInfo.EstimatedBytes - aliasInfo.BackingBytes : 0;
			snapshot.AliasedBytes += aliasInfo.BackingBytes;
			snapshot.SavedBytes += aliasInfo.SavedBytes;
		}

		auto containsResource = [](const std::vector<RenderGraph::ResourceHandle>& resources, RenderGraph::ResourceHandle resource)
			{
				return std::find(resources.begin(), resources.end(), resource) != resources.end();
			};

		auto accessState = [&](const RenderGraph::PassDesc& pass, RenderGraph::ResourceHandle resource, bool asInput)
			{
				const bool read = containsResource(pass.Reads, resource);
				const bool write = containsResource(pass.Writes, resource);
				if (read && write)
					return std::string("ReadWrite");
				return std::string(asInput ? "Read" : "Write");
			};

		std::vector<bool> culledPasses(passes.size(), false);
		for (uint32_t passIndex : compileResult.CulledPasses)
		{
			if (passIndex < culledPasses.size())
				culledPasses[passIndex] = true;
		}

		snapshot.Passes.reserve(passes.size());
		for (uint32_t passIndex = 0; passIndex < passes.size(); passIndex++)
		{
			const RenderGraph::PassDesc& pass = passes[passIndex];
			RenderGraphPassDebugInfo& passInfo = snapshot.Passes.emplace_back();
			passInfo.Index = passIndex;
			passInfo.Name = pass.Name;
			passInfo.Flags = static_cast<uint32_t>(pass.Flags);
			passInfo.Executable = static_cast<bool>(pass.Execute);
			passInfo.Culled = culledPasses[passIndex];
			if (const PassProfile* profile = findProfile(pass.Name))
			{
				passInfo.CPUTime = profile->CPUTime;
				passInfo.GPUTime = profile->GPUTime;
			}

			for (RenderGraph::ResourceHandle resource : pass.Reads)
			{
				passInfo.Inputs.push_back({ resource, accessState(pass, resource, true) });
			}

			for (RenderGraph::ResourceHandle resource : pass.Writes)
			{
				passInfo.Outputs.push_back({ resource, accessState(pass, resource, false) });
			}
		}

		snapshot.Diagnostics.reserve(compileResult.Diagnostics.size());
		for (uint32_t diagnosticIndex = 0; diagnosticIndex < compileResult.Diagnostics.size(); diagnosticIndex++)
		{
			const RenderGraph::Diagnostic& diagnostic = compileResult.Diagnostics[diagnosticIndex];
			RenderGraphDiagnosticDebugInfo& debugDiagnostic = snapshot.Diagnostics.emplace_back();
			debugDiagnostic.Severity = diagnostic.Severity;
			debugDiagnostic.Code = diagnostic.Code;
			debugDiagnostic.PassIndex = diagnostic.PassIndex;
			debugDiagnostic.PassName = diagnostic.PassName;
			debugDiagnostic.Resource = diagnostic.Resource;
			debugDiagnostic.ResourceName = diagnostic.ResourceName;
			debugDiagnostic.Message = diagnostic.Message;

			if (diagnostic.PassIndex < snapshot.Passes.size())
				snapshot.Passes[diagnostic.PassIndex].Diagnostics.push_back(diagnosticIndex);

			if (diagnostic.Resource < snapshot.Textures.size())
			{
				RenderGraphTextureDebugInfo& textureInfo = snapshot.Textures[diagnostic.Resource];
				textureInfo.DiagnosticCount++;
				if (diagnostic.Severity == RenderGraph::DiagnosticSeverity::Error)
					textureInfo.ErrorCount++;
				else if (diagnostic.Severity == RenderGraph::DiagnosticSeverity::Warning)
					textureInfo.WarningCount++;
			}
		}

		return snapshot;
	}

	void SceneRenderer::UpdateRenderGraphStatistics()
	{
		auto& memoryStats = m_Statistics.MemoryStats;
		memoryStats.RenderGraphTransientBytes = 0;
		memoryStats.RenderGraphAliasedBytes = 0;
		memoryStats.RenderGraphSavedBytes = 0;
		memoryStats.RenderGraphPassCount = 0;
		memoryStats.RenderGraphTransientCount = 0;
		memoryStats.RenderGraphAliasGroupCount = 0;

		// Reuse the executable graph already built and rendered this frame (this runs
		// from UpdateStatistics, after Build/Execute). Memory stats only read texture
		// descs and the alias plan — never names or execute callbacks — so a second
		// full BuildRenderGraph() here was pure per-frame waste.

		const auto lifetimes = m_RenderGraph.BuildAliasPlan();
		const auto& textures = m_RenderGraph.GetTextures();
		std::vector<uint64_t> aliasBytes;
		for (const RenderGraph::ResourceLifetime& lifetime : lifetimes)
		{
			if (lifetime.FirstPass == UINT32_MAX || lifetime.Resource >= textures.size())
				continue;

			const RenderGraph::TextureDesc& texture = textures[lifetime.Resource];
			if (!texture.Transient || !texture.AllowAlias)
				continue;

			const uint64_t size = Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
			memoryStats.RenderGraphTransientBytes += size;
			memoryStats.RenderGraphTransientCount++;

			if (lifetime.AliasIndex != UINT32_MAX)
			{
				if (aliasBytes.size() <= lifetime.AliasIndex)
					aliasBytes.resize(lifetime.AliasIndex + 1);
				aliasBytes[lifetime.AliasIndex] = std::max(aliasBytes[lifetime.AliasIndex], size);
			}
		}

		for (uint64_t size : aliasBytes)
			memoryStats.RenderGraphAliasedBytes += size;

		memoryStats.RenderGraphSavedBytes = memoryStats.RenderGraphTransientBytes > memoryStats.RenderGraphAliasedBytes
			? memoryStats.RenderGraphTransientBytes - memoryStats.RenderGraphAliasedBytes
			: 0;
		memoryStats.RenderGraphPassCount = static_cast<uint32_t>(m_RenderGraph.GetPasses().size());
		memoryStats.RenderGraphAliasGroupCount = static_cast<uint32_t>(aliasBytes.size());
	}

	void SceneRenderer::UpdateStatistics()
	{
		m_Statistics.DrawCalls = 0;
		m_Statistics.Meshes = 0;
		m_Statistics.SubmittedInstances = m_FrameCullingStats.SubmittedInstances;
		m_Statistics.Instances = 0;
		m_Statistics.VisibleInstances = 0;
		m_Statistics.GPUVisibleInstances = 0;
		m_Statistics.CulledInstances = 0;
		m_Statistics.FrustumCulledInstances = m_FrameCullingStats.MainViewCulledInstances;
		m_Statistics.MainViewCulledInstances = m_Statistics.FrustumCulledInstances;
		m_Statistics.ShadowCulledInstances = m_FrameCullingStats.ShadowCulledInstances;
		m_Statistics.OcclusionCulledInstances = 0;
		m_Statistics.FullyCulledInstances = m_FrameCullingStats.FullyCulledInstances;
		m_Statistics.IndirectDraws = 0;
		m_Statistics.SavedDraws = 0;
		m_Statistics.SpotlightShadowcasters = 0;
		m_Statistics.SpotlightShadowsCulled = m_FrameCullingStats.ShadowCulledInstances;

		auto accumulate = [this](const DrawCommandList& drawList, const DrawCommandOrder& drawOrder)
			{
				for (const MeshKey& key : drawOrder)
				{
					const auto drawIt = drawList.find(key);
					if (drawIt == drawList.end())
						continue;

					const StaticDrawCommand& dc = drawIt->second;
					m_Statistics.DrawCalls++;
					m_Statistics.Meshes++;
					m_Statistics.Instances += dc.InstanceCount;

					auto transformIt = m_MeshTransformMap.find(key);
					if (transformIt != m_MeshTransformMap.end())
					{
						m_Statistics.VisibleInstances += transformIt->second.VisibleInstanceCount;
						if (m_Options.EnableGPUDrivenRendering && transformIt->second.IndirectDrawOffsetBytes != std::numeric_limits<uint32_t>::max())
							m_Statistics.IndirectDraws++;
					}
					else
					{
						m_Statistics.VisibleInstances += dc.InstanceCount;
					}
				}
			};

		const MeshPassState& selectedPass = GetMeshPass(MeshPassType::SelectedMask);
		const MeshPassState& opaquePass = GetMeshPass(MeshPassType::Opaque);
		const MeshPassState& transparentPass = GetMeshPass(MeshPassType::Transparent);
		const MeshPassState& colliderPass = GetMeshPass(MeshPassType::PhysicsCollider);

		accumulate(selectedPass.DrawList, selectedPass.DrawOrder);
		accumulate(opaquePass.DrawList, opaquePass.DrawOrder);
		accumulate(transparentPass.DrawList, transparentPass.DrawOrder);

		if (m_Options.ShowPhysicsColliders)
			accumulate(colliderPass.DrawList, colliderPass.DrawOrder);

		m_Statistics.SavedDraws = m_Statistics.Instances > m_Statistics.DrawCalls
			? m_Statistics.Instances - m_Statistics.DrawCalls
			: 0;
		const uint32_t lateCulledInstances = m_Statistics.Instances > m_Statistics.VisibleInstances
			? m_Statistics.Instances - m_Statistics.VisibleInstances
			: 0;
		m_Statistics.GPUVisibleInstances = m_Statistics.VisibleInstances;
		// Real HZB occlusion rejection counts require reading the post-compute counters back from the GPU.
		m_Statistics.CulledInstances = lateCulledInstances + m_Statistics.FrustumCulledInstances + m_Statistics.OcclusionCulledInstances;

		m_Statistics.SpotlightShadowcasters = m_SpotShadowCount;

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		m_Statistics.TotalGPUTime = m_CommandBuffer->GetExecutionGPUTime(frameIndex);
		m_Statistics.PipelineStats = m_CommandBuffer->GetPipelineStatistics(frameIndex);
		const RendererConfig& rendererConfig = Renderer::GetConfig();
		if (ShouldCollectBasicRendererDiagnostics(rendererConfig))
		{
			UpdateGPUProfileTimes();
			m_Statistics.MemoryStats.RenderGraphPassCount = static_cast<uint32_t>(m_RenderGraph.GetPasses().size());
		}
		if (ShouldCollectFullRendererDiagnostics(rendererConfig))
			UpdateMemoryStatistics();
		UpdateDynamicRenderResolution();
	}

}
