#include "lpch.h"
#include "SceneRendererInternal.h"

namespace Lux {

	void SceneRenderer::UpdateGTAOData()
	{
		const bool gtaoEnabled = m_Options.EnableGTAO;
		Renderer::SetGlobalMacroInShaders("__HZ_AO_METHOD", std::to_string((int)ShaderDef::GetAOMethod(gtaoEnabled)));
		Renderer::SetGlobalMacroInShaders("__HZ_GTAO_COMPUTE_BENT_NORMALS", m_Options.GTAOBentNormals ? "1" : "0");

		m_Options.ReflectionOcclusionMethod = ShaderDef::AOMethod::None;
		Renderer::SetGlobalMacroInShaders("__HZ_REFLECTION_OCCLUSION_METHOD", std::to_string((int)m_Options.ReflectionOcclusionMethod));
	}

	void SceneRenderer::ResizeBloomResources()
	{
		if (!m_BloomComputePass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		glm::uvec2 bloomSize = GetScaledExtent({ glm::max(1u, m_ViewportWidth), glm::max(1u, m_ViewportHeight) }, m_BloomSettings.ResolutionScale);
		bloomSize.x = glm::max(m_BloomComputeWorkgroupSize, AlignUp(bloomSize.x, m_BloomComputeWorkgroupSize));
		bloomSize.y = glm::max(m_BloomComputeWorkgroupSize, AlignUp(bloomSize.y, m_BloomComputeWorkgroupSize));

		ImageViewSpecification imageViewSpec;
		imageViewSpec.MipCount = 1;

		for (uint32_t i = 0; i < (uint32_t)m_BloomComputeTextures.size(); i++)
		{
			auto& bloomTexture = m_BloomComputeTextures[i];
			bloomTexture.Texture->Resize(bloomSize);

			const uint32_t mipCount = bloomTexture.Texture->GetMipLevelCount();
			bloomTexture.ImageViews.resize(mipCount);

			imageViewSpec.Image = bloomTexture.Texture->GetImage();
			imageViewSpec.DebugName = "BloomCompute-" + std::to_string(i);

			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				imageViewSpec.Mip = mip;
				bloomTexture.ImageViews[mip] = ImageView::Create(imageViewSpec);
			}
		}
	}

	void SceneRenderer::CreateBloomPassMaterials()
	{
		if (!m_BloomComputePass || !GetSceneColorOutput() || !m_BloomComputeTextures[0].Texture)
			return;

		Ref<Image2D> inputImage = GetSceneColorOutput();
		const uint32_t mipCount = m_BloomComputeTextures[0].Texture->GetMipLevelCount();
		if (mipCount < 4)
			return;

		const uint32_t mips = mipCount - 2;

		m_BloomComputeMaterials.PrefilterMaterial = Material::Create(m_BloomComputePass->GetShader(), "Bloom-Prefilter");
		m_BloomComputeMaterials.PrefilterMaterial->Set("o_Image", m_BloomComputeTextures[0].ImageViews[0]);
		m_BloomComputeMaterials.PrefilterMaterial->Set("u_Texture", inputImage);
		m_BloomComputeMaterials.PrefilterMaterial->Set("u_BloomTexture", inputImage);

		m_BloomComputeMaterials.DownsampleAMaterials.clear();
		m_BloomComputeMaterials.DownsampleBMaterials.clear();
		m_BloomComputeMaterials.DownsampleAMaterials.resize(mips);
		m_BloomComputeMaterials.DownsampleBMaterials.resize(mips);

		for (uint32_t i = 1; i < mips; i++)
		{
			m_BloomComputeMaterials.DownsampleAMaterials[i] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-DownsampleA");
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("o_Image", m_BloomComputeTextures[1].ImageViews[i]);
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("u_Texture", m_BloomComputeTextures[0].Texture);
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("u_BloomTexture", inputImage);

			m_BloomComputeMaterials.DownsampleBMaterials[i] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-DownsampleB");
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("o_Image", m_BloomComputeTextures[0].ImageViews[i]);
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("u_Texture", m_BloomComputeTextures[1].Texture);
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("u_BloomTexture", inputImage);
		}

		m_BloomComputeMaterials.FirstUpsampleMaterial = Material::Create(m_BloomComputePass->GetShader(), "Bloom-FirstUpsample");
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("o_Image", m_BloomComputeTextures[2].ImageViews[mips - 2]);
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("u_Texture", m_BloomComputeTextures[0].Texture);
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("u_BloomTexture", inputImage);

		m_BloomComputeMaterials.UpsampleMaterials.clear();
		m_BloomComputeMaterials.UpsampleMaterials.resize(mips - 2);

		for (int32_t mip = (int32_t)mips - 3; mip >= 0; mip--)
		{
			m_BloomComputeMaterials.UpsampleMaterials[mip] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-Upsample");
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("o_Image", m_BloomComputeTextures[2].ImageViews[mip]);
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("u_Texture", m_BloomComputeTextures[0].Texture);
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("u_BloomTexture", m_BloomComputeTextures[2].ImageViews[mip + 1]);
		}
	}

	void SceneRenderer::CreatePreConvolutionPassMaterials()
	{
		if (!m_PreConvolutionComputePass || !m_SkyboxPass || !m_PreConvolutedTexture.Texture)
			return;

		const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
		m_PreConvolutionMaterials.clear();
		m_PreConvolutionMaterials.resize(mipCount);

		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			Ref<Material> material = Material::Create(m_PreConvolutionComputePass->GetShader(), "Pre-Convolution");
			material->Set("o_Image", m_PreConvolutedTexture.ImageViews[mip]);
			material->Set("u_Input", mip == 0 ? GetSceneColorOutput() : m_PreConvolutedTexture.ImageViews[mip - 1]);
			m_PreConvolutionMaterials[mip] = material;
		}
	}

	void SceneRenderer::GTAOCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "GTAO");
		if (!m_Options.EnableGTAO || !m_GTAOComputePass || !m_GTAOOutputImage)
			return;

		BeginProfiledGPU("GTAO");
		Renderer::BeginComputePass(m_CommandBuffer, m_GTAOComputePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_GTAOComputePass, nullptr, m_GTAOWorkGroups, Buffer(&m_GTAODataCB, sizeof(m_GTAODataCB)));
		Renderer::EndComputePass(m_CommandBuffer, m_GTAOComputePass);
		m_GTAOComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_GTAOOutputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_GTAOComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_GTAOEdgesOutputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GTAODenoiseCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "GTAO-Denoise");
		if (!m_Options.EnableGTAO || !m_GTAODenoisePass[0] || !m_GTAODenoisePass[1] || !m_GTAOOutputImage)
			return;

		const uint32_t denoisePasses = (uint32_t)glm::max(m_Options.GTAODenoisePasses, 0);
		if (denoisePasses == 0)
		{
			m_GTAOFinalImage = m_GTAOOutputImage;
			if (m_AOCompositePass)
			{
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			return;
		}

		m_GTAODenoiseConstants.DenoiseBlurBeta = m_GTAODataCB.DenoiseBlurBeta;
		m_GTAODenoiseConstants.ResolutionScale = m_GTAODataCB.ResolutionScale;

		BeginProfiledGPU("GTAO-Denoise");
		for (uint32_t pass = 0; pass < denoisePasses; pass++)
		{
			const uint32_t passIndex = (pass % 2u) != 0u ? 1u : 0u;
			Ref<ComputePass> denoisePass = m_GTAODenoisePass[passIndex];
			Ref<Image2D> outputImage = passIndex == 0 ? m_GTAODenoiseImage : m_GTAOOutputImage;

			Renderer::BeginComputePass(m_CommandBuffer, denoisePass);
			Renderer::DispatchCompute(m_CommandBuffer, denoisePass, nullptr, m_GTAODenoiseWorkGroups, Buffer(&m_GTAODenoiseConstants, sizeof(m_GTAODenoiseConstants)));
			Renderer::EndComputePass(m_CommandBuffer, denoisePass);
			denoisePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, outputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		m_GTAOFinalImage = (denoisePasses % 2u) != 0u ? m_GTAODenoiseImage : m_GTAOOutputImage;
		if (m_AOCompositePass)
		{
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
		if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GTAOTemporalAccumulationCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "GTAO-Temporal");
		if (!m_Options.EnableGTAO || !m_Options.EnableGTAOTemporalAccumulation)
			return;
		if (!m_GTAOTemporalPass || !m_GTAOFinalImage || !m_GTAOHistoryImages[0] || !m_GTAOHistoryImages[1])
			return;

		const uint32_t readIndex = m_GTAOHistoryIndex & 1u;
		const uint32_t writeIndex = readIndex ^ 1u;
		Ref<Image2D> historyInput = m_GTAOHistoryImages[readIndex];
		Ref<Image2D> historyOutput = m_GTAOHistoryImages[writeIndex];

		m_GTAOTemporalPass->SetInput("u_CurrentAO", m_GTAOFinalImage);
		m_GTAOTemporalPass->SetInput("u_HistoryAO", historyInput);
		m_GTAOTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_GTAOTemporalPass->SetInput("o_HistoryAO", historyOutput);

		TemporalAccumulationConstants constants;
		constants.PreviousViewProjection = m_PreviousViewProjection;
		constants.Blend = m_Options.GTAOTemporalBlend;
		constants.HasHistory = m_TemporalHistoryValid ? 1u : 0u;
		constants.BentNormals = m_Options.GTAOBentNormals ? 1u : 0u;
		constants.ResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);

		BeginProfiledGPU("GTAO-Temporal");
		Renderer::BeginComputePass(m_CommandBuffer, m_GTAOTemporalPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_GTAOTemporalPass, nullptr, m_GTAOTemporalWorkGroups, Buffer(&constants, sizeof(constants)));
		Renderer::EndComputePass(m_CommandBuffer, m_GTAOTemporalPass);
		m_GTAOTemporalPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, historyOutput, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);

		m_GTAOHistoryIndex = writeIndex;
		m_GTAOFinalImage = historyOutput;
		if (m_AOCompositePass)
		{
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
		if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
	}

	void SceneRenderer::AOComposite()
	{
		ScopedCPUProfile cpuProfile(*this, "AOComposite");
		if (!m_AOCompositePass || !m_AOCompositeMaterial || !m_GTAOFinalImage)
			return;

		BeginProfiledGPU("AOComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_AOCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AOCompositePass->GetPipeline(), m_AOCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::AODebugPass()
	{
		ScopedCPUProfile cpuProfile(*this, "AODebug");
		if (!m_AODebugPass || !m_AODebugMaterial || !m_GTAOFinalImage)
			return;

		m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
		m_AODebugPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_AODebugPass->SetInput("u_Normal", GetGeometryNormalOutput());

		BeginProfiledGPU("AODebug");
		Renderer::BeginRenderPass(m_CommandBuffer, m_AODebugPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AODebugPass->GetPipeline(), m_AODebugMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::PreConvolutionCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "PreConvolution");
		if (!m_Options.EnableSSR || !m_PreConvolutionComputePass || !m_PreConvolutedTexture.Texture || m_PreConvolutionMaterials.empty())
			return;

		struct PreConvolutionComputePushConstants
		{
			int PrevLod = 0;
			int Mode = 0;
		} pushConstants;

		Ref<Image2D> preConvolutedImage = m_PreConvolutedTexture.Texture->GetImage();
		auto transitionMip = [commandBuffer = m_CommandBuffer, preConvolutedImage](uint32_t mip, nvrhi::ResourceStates state, const char* label)
		{
			std::string markerName = std::format("Barrier PreConvolution mip {} {}", mip, label);
			Renderer::Submit([commandBuffer, preConvolutedImage, mip, state, markerName]() mutable
			{
				nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
				commandBuffer->RT_BeginMarker(markerName);
				commandList->setTextureState(preConvolutedImage->GetHandle(), nvrhi::TextureSubresourceSet(mip, 1, 0, 1), state);
				commandList->commitBarriers();
				commandBuffer->RT_EndMarker();
			});
		};

		BeginProfiledGPU("PreConvolution");
		Renderer::BeginComputePass(m_CommandBuffer, m_PreConvolutionComputePass);

		if (m_PreConvolutionMaterials[0])
		{
			auto [width, height] = m_PreConvolutedTexture.Texture->GetMipSize(0);
			const glm::uvec3 workGroups = { DivideRoundUp(glm::max(1u, width), 16u), DivideRoundUp(glm::max(1u, height), 16u), 1 };
			pushConstants.PrevLod = 0;
			pushConstants.Mode = 0;
			transitionMip(0, nvrhi::ResourceStates::UnorderedAccess, "write");
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[0], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			transitionMip(0, nvrhi::ResourceStates::ShaderResource, "read");
		}

		const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
		for (uint32_t mip = 1; mip < mipCount && mip < m_PreConvolutionMaterials.size(); mip++)
		{
			if (!m_PreConvolutionMaterials[mip])
				continue;

			auto [mipWidth, mipHeight] = m_PreConvolutedTexture.Texture->GetMipSize(mip);
			const glm::uvec3 workGroups = { DivideRoundUp(glm::max(1u, mipWidth), 16u), DivideRoundUp(glm::max(1u, mipHeight), 16u), 1 };
			pushConstants.PrevLod = (int)mip - 1;

			pushConstants.Mode = 1;
			transitionMip(mip, nvrhi::ResourceStates::UnorderedAccess, "write");
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			transitionMip(mip, nvrhi::ResourceStates::ShaderResource, "read");

			pushConstants.Mode = 2;
			transitionMip(mip, nvrhi::ResourceStates::UnorderedAccess, "write");
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			transitionMip(mip, nvrhi::ResourceStates::ShaderResource, "read");
		}

		Renderer::EndComputePass(m_CommandBuffer, m_PreConvolutionComputePass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SSRCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "SSR");
		if (!m_Options.EnableSSR || !m_SSRPass || !m_SSRImage)
			return;

		SSROptionsUB ssrOptions = m_SSROptions;
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		ssrOptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
		ssrOptions.HalfRes = ssrOptions.ResolutionScale > 1u;
		ssrOptions.TemporalAccumulation = m_Options.EnableSSRTemporalAccumulation ? 1u : 0u;
		ssrOptions.TemporalBlend = m_Options.SSRTemporalBlend;
		if (m_Options.EnableSSRTemporalAccumulation)
			ssrOptions.MaxSteps = glm::max(8, ssrOptions.MaxSteps / 2);

		if (m_SSRPass->IsInputValid("u_GTAOTex") && m_GTAOFinalImage)
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);

		BeginProfiledGPU("SSR");
		Renderer::BeginComputePass(m_CommandBuffer, m_SSRPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_SSRPass, nullptr, m_SSRWorkGroups, Buffer(&ssrOptions, sizeof(ssrOptions)));
		Renderer::EndComputePass(m_CommandBuffer, m_SSRPass);
		m_SSRPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_SSRImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_SSRFinalImage = m_SSRImage;
		if (m_SSRCompositePass)
		{
			m_SSRCompositePass->SetInput("u_SSR", m_SSRFinalImage);
			m_SSRCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_SSRCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SSRTemporalAccumulationCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "SSR-Temporal");
		if (!m_Options.EnableSSR || !m_Options.EnableSSRTemporalAccumulation)
			return;
		if (!m_SSRTemporalPass || !m_SSRImage || !m_SSRHistoryImages[0] || !m_SSRHistoryImages[1])
			return;

		const uint32_t readIndex = m_SSRHistoryIndex & 1u;
		const uint32_t writeIndex = readIndex ^ 1u;
		Ref<Image2D> historyInput = m_SSRHistoryImages[readIndex];
		Ref<Image2D> historyOutput = m_SSRHistoryImages[writeIndex];

		m_SSRTemporalPass->SetInput("u_CurrentSSR", m_SSRImage);
		m_SSRTemporalPass->SetInput("u_HistorySSR", historyInput);
		m_SSRTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_SSRTemporalPass->SetInput("o_HistorySSR", historyOutput);

		TemporalAccumulationConstants constants;
		constants.PreviousViewProjection = m_PreviousViewProjection;
		constants.Blend = m_Options.SSRTemporalBlend;
		constants.HasHistory = m_TemporalHistoryValid ? 1u : 0u;
		constants.BentNormals = 0u;
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		constants.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);

		BeginProfiledGPU("SSR-Temporal");
		Renderer::BeginComputePass(m_CommandBuffer, m_SSRTemporalPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_SSRTemporalPass, nullptr, m_SSRTemporalWorkGroups, Buffer(&constants, sizeof(constants)));
		Renderer::EndComputePass(m_CommandBuffer, m_SSRTemporalPass);
		m_SSRTemporalPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, historyOutput, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);

		m_SSRHistoryIndex = writeIndex;
		m_SSRFinalImage = historyOutput;
		if (m_SSRCompositePass)
		{
			m_SSRCompositePass->SetInput("u_SSR", m_SSRFinalImage);
			m_SSRCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_SSRCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
	}

	void SceneRenderer::SSRCompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "SSRComposite");
		if (!m_Options.EnableSSR || !m_SSRCompositePass || !m_SSRCompositeMaterial)
			return;

		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		m_SSRCompositeMaterial->Set("u_Uniforms.ResolutionScale", GetEffectResolutionDivisor(m_Options.SSRResolutionScale));
		m_SSRCompositeMaterial->Set("u_Uniforms.BilateralUpscale", UsesSSRBilateralUpscale(m_Options.SSRQuality) ? 1u : 0u);
		m_SSRCompositeMaterial->Set("u_Uniforms.QuarterDebug", m_Options.SSRQuality == SceneRendererOptions::SSRQualityPreset::QuarterDebug ? 1u : 0u);
		m_SSRCompositeMaterial->Set("u_Uniforms.DepthSigma", 0.035f);
		m_SSRCompositeMaterial->Set("u_Uniforms.NormalSigma", 32.0f);

		BeginProfiledGPU("SSRComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SSRCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SSRCompositePass->GetPipeline(), m_SSRCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::DOFPass()
	{
		ScopedCPUProfile cpuProfile(*this, "DOF");
		const RenderVolumePostProcessSettings postProcessSettings = GetResolvedPostProcessSettings();
		if (!postProcessSettings.DOFEnabled || !m_DOFPass || !m_DOFMaterial)
			return;

		const float focusDistance = glm::max(0.001f, postProcessSettings.DOFFocusDistance);
		m_DOFMaterial->Set("u_Uniforms.DOFParams", glm::vec2(focusDistance, postProcessSettings.DOFBlurSize));

		BeginProfiledGPU("DOF");
		Renderer::BeginRenderPass(m_CommandBuffer, m_DOFPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_DOFPass->GetPipeline(), m_DOFMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		if (CanCompositeDOFIntoFinalTarget())
			Renderer::CopyImage(m_CommandBuffer, m_DOFPass->GetOutput(0), m_CompositePass->GetOutput(0));

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	bool SceneRenderer::CanCompositeDOFIntoFinalTarget()
	{
		if (!GetResolvedPostProcessSettings().DOFEnabled || !m_DOFPass || !m_CompositePass)
			return false;

		Ref<Image2D> dofImage = m_DOFPass->GetOutput(0);
		Ref<Image2D> compositeImage = m_CompositePass->GetOutput(0);
		if (!dofImage || !compositeImage)
			return false;

		return dofImage->GetSize() == compositeImage->GetSize();
	}

	void SceneRenderer::JumpFloodPass()
	{
		ScopedCPUProfile cpuProfile(*this, "JumpFlood");
		if (!m_Options.EnableJumpFlood || !m_JumpFloodInitPass || !m_JumpFloodInitMaterial || !m_JumpFloodPasses[0] || !m_JumpFloodPasses[1])
			return;

		BeginProfiledGPU("JumpFlood");
		Renderer::BeginRenderPass(m_CommandBuffer, m_JumpFloodInitPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_JumpFloodInitPass->GetPipeline(), m_JumpFloodInitMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		int step = 4;
		uint32_t passIndex = 0;
		Ref<Image2D> input = m_JumpFloodInitPass->GetOutput(0);
		Ref<Image2D> output = input;

		Ref<Framebuffer> passFramebuffer = m_JumpFloodPasses[0]->GetTargetFramebuffer();
		const glm::vec2 texelSize = {
			passFramebuffer && passFramebuffer->GetWidth() > 0 ? 1.0f / (float)passFramebuffer->GetWidth() : 1.0f,
			passFramebuffer && passFramebuffer->GetHeight() > 0 ? 1.0f / (float)passFramebuffer->GetHeight() : 1.0f
		};

		Buffer vertexOverrides;
		vertexOverrides.Allocate(sizeof(glm::vec2) + sizeof(int));
		vertexOverrides.Write(glm::value_ptr(texelSize), sizeof(glm::vec2));

		while (step > 0)
		{
			Ref<RenderPass> jumpFloodPass = m_JumpFloodPasses[passIndex];
			if (!jumpFloodPass || !m_JumpFloodPassMaterials[passIndex])
				break;

			jumpFloodPass->SetInput("u_Texture", input);
			vertexOverrides.Write(&step, sizeof(int), sizeof(glm::vec2));

			Renderer::BeginRenderPass(m_CommandBuffer, jumpFloodPass);
			Renderer::SubmitFullscreenQuadWithOverrides(m_CommandBuffer, jumpFloodPass->GetPipeline(), m_JumpFloodPassMaterials[passIndex], vertexOverrides, Buffer());
			Renderer::EndRenderPass(m_CommandBuffer);

			output = jumpFloodPass->GetOutput(0);
			input = output;
			passIndex = (passIndex + 1u) % 2u;
			step /= 2;
		}

		vertexOverrides.Release();

		if (m_JumpFloodCompositePass && output)
			m_JumpFloodCompositePass->SetInput("u_Texture", output);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::JumpFloodCompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "JumpFloodComposite");
		if (!m_Options.EnableJumpFlood || !m_JumpFloodCompositePass || !m_JumpFloodCompositeMaterial)
			return;

		BeginProfiledGPU("JumpFloodComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_JumpFloodCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_JumpFloodCompositePass->GetPipeline(), m_JumpFloodCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::BloomCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "BloomCompute");
		const RenderVolumePostProcessSettings postProcessSettings = GetResolvedPostProcessSettings();
		if (!postProcessSettings.BloomEnabled || !m_BloomComputePass || !m_BloomComputePipeline || !m_BloomComputeMaterials.PrefilterMaterial)
			return;

		const uint32_t mipCount = m_BloomComputeTextures[0].Texture->GetMipLevelCount();
		if (mipCount < 4)
			return;

		const uint32_t mips = mipCount - 2;
		if (mips < 3 || !m_BloomComputeMaterials.FirstUpsampleMaterial)
			return;

		struct BloomComputePushConstants
		{
			glm::vec4 Params;
			glm::vec4 TexSize;
			float LOD = 0.0f;
			int Mode = 0;
		} pushConstants;

		const float knee = glm::max(postProcessSettings.BloomKnee, 0.0001f);
		pushConstants.Params = {
			postProcessSettings.BloomThreshold,
			postProcessSettings.BloomThreshold - knee,
			knee * 2.0f,
			0.25f / knee
		};

		auto setTexSize = [&](uint32_t mip)
		{
			auto [mipWidth, mipHeight] = m_BloomComputeTextures[0].Texture->GetMipSize(mip);
			pushConstants.TexSize = {
				(float)mipWidth,
				(float)mipHeight,
				mipWidth > 0 ? 1.0f / (float)mipWidth : 0.0f,
				mipHeight > 0 ? 1.0f / (float)mipHeight : 0.0f
			};
		};

		auto dispatchForMip = [&](Ref<Material> material, uint32_t mip)
		{
			if (!material)
				return;

			auto [mipWidth, mipHeight] = m_BloomComputeTextures[0].Texture->GetMipSize(mip);
			const glm::uvec3 workGroups = {
				AlignUp(glm::max(1u, mipWidth), m_BloomComputeWorkgroupSize) / m_BloomComputeWorkgroupSize,
				AlignUp(glm::max(1u, mipHeight), m_BloomComputeWorkgroupSize) / m_BloomComputeWorkgroupSize,
				1
			};
			Renderer::DispatchCompute(m_CommandBuffer, m_BloomComputePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
		};

		auto transitionBloomMip = [commandBuffer = m_CommandBuffer, this](uint32_t textureIndex, uint32_t mip, nvrhi::ResourceStates state, const char* label)
		{
			if (textureIndex >= m_BloomComputeTextures.size() || !m_BloomComputeTextures[textureIndex].Texture)
				return;

			Ref<Image2D> image = m_BloomComputeTextures[textureIndex].Texture->GetImage();
			std::string markerName = std::format("Barrier Bloom {} mip {} {}", textureIndex, mip, label);
			Renderer::Submit([commandBuffer, image, mip, state, markerName]() mutable
			{
				nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
				commandBuffer->RT_BeginMarker(markerName);
				commandList->setTextureState(image->GetHandle(), nvrhi::TextureSubresourceSet(mip, 1, 0, 1), state);
				commandList->commitBarriers();
				commandBuffer->RT_EndMarker();
			});
		};

		BeginProfiledGPU("BloomCompute");
		Renderer::BeginComputePass(m_CommandBuffer, m_BloomComputePass);

		// Prefilter
		pushConstants.Mode = 0;
		pushConstants.LOD = 0.0f;
		setTexSize(0);
		transitionBloomMip(0, 0, nvrhi::ResourceStates::UnorderedAccess, "write");
		dispatchForMip(m_BloomComputeMaterials.PrefilterMaterial, 0);
		transitionBloomMip(0, 0, nvrhi::ResourceStates::ShaderResource, "read");

		// Downsample, ping-ponging between texture 0 and texture 1.
		pushConstants.Mode = 1;
		for (uint32_t i = 1; i < mips; i++)
		{
			setTexSize(i);
			pushConstants.LOD = (float)i - 1.0f;
			transitionBloomMip(1, i, nvrhi::ResourceStates::UnorderedAccess, "write");
			dispatchForMip(m_BloomComputeMaterials.DownsampleAMaterials[i], i);
			transitionBloomMip(1, i, nvrhi::ResourceStates::ShaderResource, "read");

			pushConstants.LOD = (float)i;
			transitionBloomMip(0, i, nvrhi::ResourceStates::UnorderedAccess, "write");
			dispatchForMip(m_BloomComputeMaterials.DownsampleBMaterials[i], i);
			transitionBloomMip(0, i, nvrhi::ResourceStates::ShaderResource, "read");
		}

		// First upsample from the smallest downsampled mip.
		pushConstants.Mode = 2;
		pushConstants.LOD = (float)mips - 2.0f;
		setTexSize(mips - 1);
		transitionBloomMip(2, mips - 2, nvrhi::ResourceStates::UnorderedAccess, "write");
		dispatchForMip(m_BloomComputeMaterials.FirstUpsampleMaterial, mips - 2);
		transitionBloomMip(2, mips - 2, nvrhi::ResourceStates::ShaderResource, "read");

		// Upsample back to mip 0.
		pushConstants.Mode = 3;
		for (int32_t mip = (int32_t)mips - 3; mip >= 0; mip--)
		{
			pushConstants.LOD = (float)mip;
			setTexSize((uint32_t)mip + 1u);
			transitionBloomMip(2, (uint32_t)mip, nvrhi::ResourceStates::UnorderedAccess, "write");
			dispatchForMip(m_BloomComputeMaterials.UpsampleMaterials[mip], (uint32_t)mip);
			transitionBloomMip(2, (uint32_t)mip, nvrhi::ResourceStates::ShaderResource, "read");
		}

		Renderer::EndComputePass(m_CommandBuffer, m_BloomComputePass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::AutoExposurePass()
	{
		ScopedCPUProfile cpuProfile(*this, "AutoExposurePass");
		if (!m_LuminanceHistogramPass || !m_LuminanceAveragePass)
			return;

		Ref<Image2D> sceneColor = GetSceneColorOutput();
		if (!sceneColor)
			return;

		const RenderVolumePostProcessSettings settings = GetResolvedPostProcessSettings();
		if (settings.ExposureControl != ExposureMode::Automatic)
			return;

		const uint32_t width = glm::max(1u, sceneColor->GetWidth());
		const uint32_t height = glm::max(1u, sceneColor->GetHeight());
		const float minLog = s_AutoExposureMinLogLuminance;
		const float maxLog = s_AutoExposureMaxLogLuminance;
		const float logRange = maxLog - minLog;

		BeginProfiledGPU("AutoExposurePass");

		// 1) Build the log-luminance histogram of the HDR scene color.
		struct HistogramPushConstants
		{
			float MinLogLuminance;
			float InverseLogLuminanceRange;
			uint32_t InputWidth;
			uint32_t InputHeight;
		} histogramPush;
		histogramPush.MinLogLuminance = minLog;
		histogramPush.InverseLogLuminanceRange = logRange > 0.0f ? 1.0f / logRange : 0.0f;
		histogramPush.InputWidth = width;
		histogramPush.InputHeight = height;

		const glm::uvec3 histogramGroups = { AlignUp(width, 16u) / 16u, AlignUp(height, 16u) / 16u, 1u };
		Renderer::BeginComputePass(m_CommandBuffer, m_LuminanceHistogramPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_LuminanceHistogramPass, nullptr, histogramGroups, Buffer(&histogramPush, sizeof(histogramPush)));
		m_LuminanceHistogramPass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSLuminanceHistogram->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(m_CommandBuffer, m_LuminanceHistogramPass);

		// 2) Reduce to an average luminance, temporally adapt, write the exposure
		//    multiplier, and clear the histogram for the next frame.
		struct AveragePushConstants
		{
			float MinLogLuminance;
			float LogLuminanceRange;
			float TimeDelta;
			float SpeedUp;
			float SpeedDown;
			float MinEV100;
			float MaxEV100;
			uint32_t PixelCount;
		} averagePush;
		averagePush.MinLogLuminance = minLog;
		averagePush.LogLuminanceRange = logRange;
		averagePush.TimeDelta = Application::Get().GetFrametime().GetSeconds();
		averagePush.SpeedUp = glm::max(settings.AutoAdaptationSpeedUp, 0.0f);
		averagePush.SpeedDown = glm::max(settings.AutoAdaptationSpeedDown, 0.0f);
		averagePush.MinEV100 = settings.AutoMinEV100;
		averagePush.MaxEV100 = glm::max(settings.AutoMaxEV100, settings.AutoMinEV100);
		averagePush.PixelCount = width * height;

		Renderer::BeginComputePass(m_CommandBuffer, m_LuminanceAveragePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_LuminanceAveragePass, nullptr, { 1u, 1u, 1u }, Buffer(&averagePush, sizeof(averagePush)));
		m_LuminanceAveragePass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSExposureState->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_LuminanceAveragePass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSLuminanceHistogram->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(m_CommandBuffer, m_LuminanceAveragePass);

		m_AutoExposureValid = true;
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::TAAResolvePass()
	{
		ScopedCPUProfile cpuProfile(*this, "TAA");
		if (!m_Options.EnableTAA || !m_TAAResolvePass || !m_TAAHistoryImages[0] || !m_TAAHistoryImages[1])
			return;

		Ref<Image2D> sceneColor = GetSceneColorOutput();
		if (!sceneColor)
			return;

		const uint32_t readIndex = m_TAAHistoryIndex & 1u;
		const uint32_t writeIndex = readIndex ^ 1u;
		Ref<Image2D> historyInput = m_TAAHistoryImages[readIndex];
		Ref<Image2D> historyOutput = m_TAAHistoryImages[writeIndex];

		m_TAAResolvePass->SetInput("u_SceneColor", sceneColor);
		m_TAAResolvePass->SetInput("u_History", historyInput);
		m_TAAResolvePass->SetInput("u_Velocity", GetGeometryVelocityOutput());
		m_TAAResolvePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_TAAResolvePass->SetInput("o_Resolved", historyOutput);

		struct TAAPushConstants
		{
			float Blend;
			uint32_t HasHistory;
			float Sharpness;
			uint32_t Padding1;
		} push;
		push.Blend = glm::clamp(m_Options.TAAHistoryBlend, 0.0f, 0.98f);
		push.HasHistory = m_TemporalHistoryValid ? 1u : 0u;
		push.Sharpness = glm::max(m_Options.TAASharpness, 0.0f);
		push.Padding1 = 0u;

		const glm::uvec3 groups = {
			(glm::max(1u, sceneColor->GetWidth()) + 7u) / 8u,
			(glm::max(1u, sceneColor->GetHeight()) + 7u) / 8u,
			1u
		};

		BeginProfiledGPU("TAA");
		Renderer::BeginComputePass(m_CommandBuffer, m_TAAResolvePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_TAAResolvePass, nullptr, groups, Buffer(&push, sizeof(push)));
		Renderer::EndComputePass(m_CommandBuffer, m_TAAResolvePass);
		m_TAAResolvePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, historyOutput, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		// Copy the resolved frame back into scene color so downstream passes
		// (bloom / auto-exposure / composite) consume the anti-aliased result.
		Renderer::CopyImage(m_CommandBuffer, historyOutput, sceneColor);

		m_TAAHistoryIndex = writeIndex;
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	float SceneRenderer::ComputeFinalExposure(const RenderVolumePostProcessSettings& settings) const
	{
		switch (settings.ExposureControl)
		{
			case ExposureMode::ManualEV:
				return Exposure::ExposureFromEV100(settings.ExposureEV100 - settings.ExposureCompensation);
			case ExposureMode::Camera:
				return Exposure::ExposureFromCamera(settings.Aperture, settings.ShutterSpeed, settings.ISO, settings.ExposureCompensation);
			case ExposureMode::Automatic:
				// Driven by the histogram auto-exposure passes; falls back to the manual
				// multiplier until the first auto-exposure result is available.
				return m_AutoExposureValid ? m_AutoExposure : settings.Exposure;
			case ExposureMode::Manual:
			default:
				return settings.Exposure;
		}
	}

}
