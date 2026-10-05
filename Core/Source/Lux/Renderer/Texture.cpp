// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Texture.h"

#include "Lux/Renderer/RendererAPI.h"
#include "Lux/Renderer/Renderer.h"

#include "Lux/Asset/TextureImporter.h"

#include <atomic>

namespace Lux {

	namespace Utils {

		static nvrhi::SamplerAddressMode NVRHISamplerWrap(TextureWrap wrap)
		{
			switch (wrap)
			{
			case TextureWrap::Clamp:   return nvrhi::SamplerAddressMode::Clamp;
			case TextureWrap::Repeat:  return nvrhi::SamplerAddressMode::Repeat;
			}
			LUX_CORE_ASSERT(false, "Unknown wrap mode");
			return (nvrhi::SamplerAddressMode)0;
		}

		static bool NVRHISamplerFilter(TextureFilter filter)
		{
			switch (filter)
			{
			case TextureFilter::Linear:
			case TextureFilter::Cubic:   return true;
			case TextureFilter::Nearest: return false;
			}
			LUX_CORE_ASSERT(false, "Unknown filter");
			return false;
		}

		static size_t GetMemorySize(ImageFormat format, uint32_t width, uint32_t height)
		{
			return ::Lux::Utils::GetImageMemorySize(format, width, height);
		}

		static bool ValidateSpecification(const TextureSpecification& specification)
		{
			bool result = true;

			result = specification.Width > 0 && specification.Height > 0 && specification.Width < 65536 && specification.Height < 65536;
			LUX_CORE_VERIFY(result);

			return result;
		}

	}

	//////////////////////////////////////////////////////////////////////////////////
	// Texture2D
	//////////////////////////////////////////////////////////////////////////////////

	Texture2D::Texture2D(const TextureSpecification& specification, const std::filesystem::path& filepath)
		: m_Specification(specification), m_Path(filepath)
	{
		if (m_Specification.DebugName.empty())
			m_Specification.DebugName = filepath.string();
		CreateFromFile(specification, filepath);
	}

	Texture2D::Texture2D(const TextureSpecification& specification, Buffer data)
		: m_Specification(specification)
	{
		CreateFromBuffer(specification, data);
	}

	Texture2D::~Texture2D()
	{
		//	if (m_Image)
		//		m_Image->Release();

		m_ImageData.Release();
	}

	Ref<Texture2D> Texture2D::CreateFromSRGB(Ref<Texture2D> texture)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// The source texture should already have mipmaps generated (it's RGBA, which supports compute-based mip generation)
		// We copy all mip levels from the source and disable mip generation on the SRGBA texture
		// because SRGB formats cannot be used as storage images in compute shaders

		uint32_t mipCount = texture->GetMipLevelCount();

		TextureSpecification spec;
		spec.Width = texture->GetWidth();
		spec.Height = texture->GetHeight();
		spec.Format = ImageFormat::SRGBA;
		spec.GenerateMips = false; // SRGBA cannot be used as storage image for compute-based mip generation
		spec.SamplerWrap = texture->m_Specification.SamplerWrap;
		spec.SamplerFilter = texture->m_Specification.SamplerFilter;
		spec.MaxAnisotropy = texture->m_Specification.MaxAnisotropy;

		// Create the SRGBA image with all mip levels pre-allocated (matching source texture)
		ImageSpecification imageSpec;
		imageSpec.Format = ImageFormat::SRGBA;
		imageSpec.Width = spec.Width;
		imageSpec.Height = spec.Height;
		imageSpec.Mips = mipCount; // Allocate all mip levels
		imageSpec.CreateSampler = false;
		imageSpec.Transfer = true;

		Ref<Image2D> srgbImage = Image2D::Create(imageSpec);
		srgbImage->Invalidate();

		Ref<Texture2D> srgbTexture = Ref<Texture2D>::Create(spec);
		srgbTexture->m_Image = srgbImage;

		Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, "CreateFromSRGB-CopyMips");
		commandBuffer->Begin();

		Renderer::Submit([srgbImage, srgbTexture, mipCount, spec, texture, commandBuffer]() mutable
			{

				nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
				nvrhi::SamplerDesc samplerDesc;
				samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = Utils::NVRHISamplerFilter(spec.SamplerFilter);
				samplerDesc.addressU = Utils::NVRHISamplerWrap(spec.SamplerWrap);
				samplerDesc.addressV = samplerDesc.addressW = samplerDesc.addressU;
				samplerDesc.maxAnisotropy = spec.MaxAnisotropy;
				srgbImage->GetImageInfo().Sampler = device->createSampler(samplerDesc);

				// Copy all mip levels from source texture to SRGBA texture using GPU copy
				nvrhi::CommandListHandle commandList = commandBuffer->GetActive();

				// Copy each mip level from source to destination
				for (uint32_t mip = 0; mip < mipCount; mip++)
				{
					nvrhi::TextureSlice srcSlice;
					srcSlice.mipLevel = mip;

					nvrhi::TextureSlice dstSlice;
					dstSlice.mipLevel = mip;

					commandList->copyTexture(srgbTexture->GetHandle(), dstSlice, texture->GetHandle(), srcSlice);
				}

			});

		commandBuffer->End();
		commandBuffer->Submit();

		return srgbTexture;
	}

	void Texture2D::CreateFromFile(const TextureSpecification& specification, const std::filesystem::path& filepath)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Utils::ValidateSpecification(specification);

		::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Info, "loading image {}", filepath);

		//specification.GenerateMips = true;

		TextureImportSettings importSettings;
		importSettings.FlipVertically = m_Specification.FlipVertically;
		importSettings.GenerateMips = m_Specification.GenerateMips;
		importSettings.Compression = m_Specification.Compression;
		importSettings.MipPolicy = m_Specification.MipPolicy;
		importSettings.MipBias = m_Specification.MipBias;
		m_ImageData = TextureImporter::ToBufferFromFile(filepath, m_Specification.Format, m_Specification.Width, m_Specification.Height, importSettings);
		if (!m_ImageData)
		{
			// TODO(Yan): move this to asset manager
			LUX_CORE_ERROR("Failed to load texture from file: {}", filepath);
			m_ImageData = TextureImporter::ToBufferFromFile("Resources/Textures/ErrorTexture.png", m_Specification.Format, m_Specification.Width, m_Specification.Height, m_Specification.FlipVertically);
		}
		if (Utils::IsBlockCompressed(m_Specification.Format))
			m_Specification.GenerateMips = false;

		ImageSpecification imageSpec;
		imageSpec.DebugName = m_Specification.DebugName;
		imageSpec.Format = m_Specification.Format;
		imageSpec.Width = m_Specification.Width;
		imageSpec.Height = m_Specification.Height;
		imageSpec.Mips = m_Specification.GenerateMips ? GetMipLevelCount() : 1;
		imageSpec.DebugName = specification.DebugName;
		imageSpec.CreateSampler = false;
		imageSpec.MipBias = specification.MipBias;
		m_Image = Image2D::Create(imageSpec);

		LUX_CORE_ASSERT(m_Specification.Format != ImageFormat::None);

		Invalidate();
	}

	void Texture2D::ReplaceFromFile(const TextureSpecification& specification, const std::filesystem::path& filepath)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Utils::ValidateSpecification(specification);

		TextureImportSettings importSettings;
		importSettings.FlipVertically = m_Specification.FlipVertically;
		importSettings.GenerateMips = m_Specification.GenerateMips;
		importSettings.Compression = m_Specification.Compression;
		importSettings.MipPolicy = m_Specification.MipPolicy;
		importSettings.MipBias = m_Specification.MipBias;
		m_ImageData = TextureImporter::ToBufferFromFile(filepath, m_Specification.Format, m_Specification.Width, m_Specification.Height, importSettings);
		if (!m_ImageData)
		{
			// TODO(Yan): move this to asset manager
			LUX_CORE_ERROR("Failed to load texture from file: {}", filepath);
			m_ImageData = TextureImporter::ToBufferFromFile("Resources/Textures/ErrorTexture.png", m_Specification.Format, m_Specification.Width, m_Specification.Height, m_Specification.FlipVertically);
		}
		if (Utils::IsBlockCompressed(m_Specification.Format))
			m_Specification.GenerateMips = false;

		ImageSpecification imageSpec;
		imageSpec.Format = m_Specification.Format;
		imageSpec.Width = m_Specification.Width;
		imageSpec.Height = m_Specification.Height;
		imageSpec.Mips = m_Specification.GenerateMips ? GetMipLevelCount() : 1;
		imageSpec.DebugName = specification.DebugName;
		imageSpec.CreateSampler = false;
		imageSpec.MipBias = specification.MipBias;
		m_Image = Image2D::Create(imageSpec);

		LUX_CORE_ASSERT(m_Specification.Format != ImageFormat::None);

		Ref<Texture2D> instance = this;
		Renderer::Submit([instance]() mutable
			{
				instance->Invalidate();
			});
	}

	void Texture2D::CreateFromBuffer(const TextureSpecification& specification, Buffer data)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Info, "loading image from buffer");


		if (m_Specification.Height == 0)
		{
			TextureImportSettings importSettings;
			importSettings.FlipVertically = m_Specification.FlipVertically;
			importSettings.GenerateMips = m_Specification.GenerateMips;
			importSettings.Compression = m_Specification.Compression;
			importSettings.MipPolicy = m_Specification.MipPolicy;
			importSettings.MipBias = m_Specification.MipBias;
			m_ImageData = TextureImporter::ToBufferFromMemory(Buffer(data.Data, m_Specification.Width), m_Specification.Format, m_Specification.Width, m_Specification.Height, importSettings);
			if (!m_ImageData)
			{
				// TODO(Yan): move this to asset manager
				m_ImageData = TextureImporter::ToBufferFromFile("Resources/Textures/ErrorTexture.png", m_Specification.Format, m_Specification.Width, m_Specification.Height, m_Specification.FlipVertically);
			}

			Utils::ValidateSpecification(m_Specification);
		}
		else if (data)
		{
			Utils::ValidateSpecification(m_Specification);
			auto size = (uint32_t)Utils::GetMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height);
			m_ImageData = Buffer::Copy(data.Data, size);
		}
		else
		{
			Utils::ValidateSpecification(m_Specification);
			auto size = (uint32_t)Utils::GetMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height);
			m_ImageData.Allocate(size);
			m_ImageData.ZeroInitialize();
		}
		if (Utils::IsBlockCompressed(m_Specification.Format))
			m_Specification.GenerateMips = false;

		ImageSpecification imageSpec;
		imageSpec.Format = m_Specification.Format;
		imageSpec.Width = m_Specification.Width;
		imageSpec.Height = m_Specification.Height;
		imageSpec.Mips = m_Specification.GenerateMips ? Texture2D::GetMipLevelCount() : 1;
		imageSpec.DebugName = specification.DebugName;
		imageSpec.CreateSampler = false;
		imageSpec.MipBias = specification.MipBias;
		if (specification.Storage)
			imageSpec.Usage = ImageUsage::Storage;
		m_Image = Image2D::Create(imageSpec);


		Invalidate();
	}

	void Texture2D::Resize(const glm::uvec2& size)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Resize(size.x, size.y);
	}

	void Texture2D::Resize(const uint32_t width, const uint32_t height)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Specification.Width = width;
		m_Specification.Height = height;

		Invalidate();

		/*Ref<Texture2D> instance = this;
		Renderer::Submit([instance]() mutable
		{
			instance->Invalidate();
		});*/
	}

	void Texture2D::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		m_Image->Release();

		uint32_t mipCount = m_Specification.GenerateMips ? GetMipLevelCount() : 1;

		ImageSpecification& imageSpec = m_Image->GetSpecification();
		imageSpec.Format = m_Specification.Format;
		imageSpec.Width = m_Specification.Width;
		imageSpec.Height = m_Specification.Height;
		imageSpec.Mips = mipCount;
		imageSpec.CreateSampler = false;
		imageSpec.Transfer = true;
		if (!m_ImageData) // TODO(Yan): better management for this, probably from texture spec
			imageSpec.Usage = ImageUsage::Storage;

		Ref<Image2D> image = m_Image.As<Image2D>();
		image->RT_Invalidate();

		auto& info = image->GetImageInfo();

		if (m_ImageData)
		{
			SetData(m_ImageData);
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// CREATE TEXTURE SAMPLER (owned by Image)
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = Utils::NVRHISamplerFilter(m_Specification.SamplerFilter);
		samplerDesc.addressU = Utils::NVRHISamplerWrap(m_Specification.SamplerWrap);
		samplerDesc.addressV = samplerDesc.addressW = samplerDesc.addressU;
		samplerDesc.maxAnisotropy = m_Specification.MaxAnisotropy;
		samplerDesc.mipBias = m_Specification.MipBias;

		info.Sampler = device->createSampler(samplerDesc);


		if (m_ImageData && m_Specification.GenerateMips && mipCount > 1)
			GenerateMips();

		// TODO(Yan): option for local storage
		m_ImageData.Release();
		m_ImageData = Buffer();
	}

	void Texture2D::SetData(Buffer buffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Image->SetData(buffer);

		// Generate mips
		uint32_t mipCount = m_Specification.GenerateMips ? GetMipLevelCount() : 1;
		if (m_Specification.GenerateMips && mipCount > 1)
			GenerateMips();
	}

	void Texture2D::Lock()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_ImageData)
		{
			auto size = (uint32_t)Utils::GetMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height);
			m_ImageData.Allocate(size);
		}
	}

	void Texture2D::Unlock()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		SetData(m_ImageData);
	}

	Buffer Texture2D::GetWriteableBuffer()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_ImageData;
	}

	const std::filesystem::path& Texture2D::GetPath() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Path;
	}

	uint32_t Texture2D::GetMipLevelCount() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Utils::CalculateMipCount(m_Specification.Width, m_Specification.Height);
	}

	std::pair<uint32_t, uint32_t> Texture2D::GetMipSize(uint32_t mip) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t width = m_Specification.Width;
		uint32_t height = m_Specification.Height;
		while (mip != 0)
		{
			width /= 2;
			height /= 2;
			mip--;
		}

		return { width, height };
	}

	void Texture2D::GenerateMips()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// SRGB/SRGBA formats cannot be used as storage images in compute shaders
		// Skip compute-based mip generation for these formats
		if (m_Specification.Format == ImageFormat::SRGB || m_Specification.Format == ImageFormat::SRGBA)
		{
			LUX_CORE_WARN("Texture2D::GenerateMips - Skipping compute-based mip generation for SRGB/SRGBA format texture '{}'. "
				"SRGB formats do not support storage image operations required for compute shaders.", m_Specification.DebugName);
			return;
		}
		if (Utils::IsBlockCompressed(m_Specification.Format))
		{
			LUX_CORE_WARN("Texture2D::GenerateMips - Skipping compute-based mip generation for block-compressed texture '{}'. "
				"Compressed mip chains must be authored by the import pipeline.", m_Specification.DebugName);
			return;
		}

		Ref<RenderCommandBuffer> renderCommandBuffer = RenderCommandBuffer::Create(1, std::format("Texture2D::GenerateMips - {}", m_Specification.DebugName));

		Ref<Shader> shader = Renderer::GetShaderLibrary()->Get(Utils::IsIntegerBased(m_Specification.Format) ? "LinearSampleUInt" : "LinearSample");

		ComputePassSpecification spec;
		spec.DebugName = "LinearSample";
		spec.Pipeline = Renderer::GetOrCreateMipGenPipeline(shader); // cached: built once, not per texture
		Ref<ComputePass> computePass = ComputePass::Create(spec);

		renderCommandBuffer->Begin();

		Ref<Texture2D> instance = this;
		Renderer::Submit([renderCommandBuffer, instance]()
			{
				LUX_CORE_WARN("{}: Generating mips for format {}", instance->m_Specification.DebugName, Utils::ImageFormatToString(instance->m_Specification.Format));

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				commandList->setTextureState(instance->m_Image->GetImageInfo().ImageHandle, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
				commandList->commitBarriers();
			});

		Renderer::BeginComputePass(renderCommandBuffer, computePass);

		struct PushConstants
		{
			glm::vec2 TexelSize;
			int SourceMip;
		} pushConstants;

		ImageViewSpecification srcImageViewSpec;
		srcImageViewSpec.Image = m_Image;
		srcImageViewSpec.MipCount = 1;

		ImageViewSpecification dstImageViewSpec = srcImageViewSpec;

		float targetMipWidth = static_cast<float>(m_Specification.Width);
		float targetMipHeight = static_cast<float>(m_Specification.Height);

		const auto mipLevels = GetMipLevelCount();
		for (uint32_t mip = 1; mip < mipLevels; mip++)
		{
			targetMipWidth /= 2;
			targetMipHeight /= 2;

			dstImageViewSpec.Mip = mip;
			srcImageViewSpec.Mip = mip - 1;

			Ref<ImageView> srcImageView = ImageView::Create(srcImageViewSpec);
			Ref<ImageView> dstImageView = ImageView::Create(dstImageViewSpec);

			Ref<Material> material = Material::Create(shader);

			material->Set("u_InputTexture", srcImageView);
			material->Set("o_OutputTexture", dstImageView);

			pushConstants.TexelSize = { 1.0f / targetMipWidth , 1.0f / targetMipHeight };
			pushConstants.SourceMip = mip - 1;

			glm::uvec3 workGroups{ targetMipWidth / 8, targetMipHeight / 8, 1 };
			workGroups = glm::max(workGroups, { 1 });
			Renderer::DispatchCompute(renderCommandBuffer, computePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));

			Renderer::Submit([renderCommandBuffer, instance]()
				{
					nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
					commandList->commitBarriers();
				});
		}

		Renderer::EndComputePass(renderCommandBuffer, computePass);

		Renderer::Submit([renderCommandBuffer, instance]()
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				commandList->setTextureState(instance->m_Image->GetImageInfo().ImageHandle, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
				commandList->commitBarriers();
			});

		renderCommandBuffer->End();
		renderCommandBuffer->Submit();

	}

	void Texture2D::CopyToHostBuffer(Buffer& buffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_Image)
			m_Image->CopyToHostBuffer(buffer);
	}

	//////////////////////////////////////////////////////////////////////////////////
	// TextureCube
	//////////////////////////////////////////////////////////////////////////////////

	// Live-cube count for the create/destroy trace logs. Cubes are created on the main and asset
	// threads and destroyed wherever their last Ref drops (often the render thread), so this is
	// atomic; it replaced an unsynchronised map that was only ever used for its size.
	static std::atomic<uint32_t> s_LiveTextureCubes{ 0 };

	TextureCube::TextureCube(const TextureSpecification& specification, Buffer data)
		: m_Specification(specification)
	{
		if (data)
		{
			uint32_t size = m_Specification.Width * m_Specification.Height * 4 * 6; // six layers
			m_LocalStorage = Buffer::Copy(data.Data, size);
		}

		Invalidate();
		//Ref<TextureCube> instance = this;
		//Renderer::Submit([instance]() mutable
		//{
		//	instance->Invalidate();
		//});
	}

	void TextureCube::Release()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_Image == nullptr)
			return;

		s_LiveTextureCubes.fetch_sub(1, std::memory_order_relaxed);
		m_Image = nullptr;
	}

	TextureCube::~TextureCube()
	{
		LUX_CORE_TRACE_TAG("Renderer", "Destroying TextureCube (live cubes: {})", s_LiveTextureCubes.load(std::memory_order_relaxed));
		Release();
	}

	void TextureCube::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Release();

		uint32_t mipCount = m_Specification.GenerateMips ? GetMipLevelCount() : 1;

		ImageSpecification imageSpec;
		imageSpec.DebugName = m_Specification.DebugName;
		imageSpec.Dimension = nvrhi::TextureDimension::TextureCube;
		imageSpec.Format = m_Specification.Format;
		imageSpec.Usage = ImageUsage::Storage;
		imageSpec.Width = m_Specification.Width;
		imageSpec.Height = m_Specification.Height;
		imageSpec.Mips = mipCount;
		imageSpec.Layers = 6;
		imageSpec.MipBias = m_Specification.MipBias;

		m_Image = Image2D::Create(imageSpec);
		m_Image->RT_Invalidate();

		const uint32_t liveCubes = s_LiveTextureCubes.fetch_add(1, std::memory_order_relaxed) + 1;
		LUX_CORE_TRACE_TAG("Renderer", "Creating TextureCube (live cubes: {})", liveCubes);

		if (m_LocalStorage)
		{
			// Shared upload batch — see Renderer::RecordResourceUpload.
			Renderer::RecordResourceUpload([&](nvrhi::ICommandList* uploadList)
			{
				// NOTE(Yan): ONLY WORKS FOR MIP 0!
				const uint8_t* data = m_LocalStorage.As<uint8_t>();
				uint64_t stride = m_LocalStorage.Size / 6;
				for (uint32_t i = 0; i < 6; i++)
				{
					uploadList->writeTexture(GetHandle(), i, 0, data, Utils::GetImageMemoryRowPitch(m_Specification.Format, m_Specification.Width));
					data += stride;
				}
			});
		}


	}

	uint32_t TextureCube::GetMipLevelCount() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Utils::CalculateMipCount(m_Specification.Width, m_Specification.Height);
	}

	std::pair<uint32_t, uint32_t> TextureCube::GetMipSize(uint32_t mip) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t width = m_Specification.Width;
		uint32_t height = m_Specification.Height;
		while (mip != 0)
		{
			width /= 2;
			height /= 2;
			mip--;
		}

		return { width, height };
	}

	nvrhi::TextureSubresourceSet TextureCube::CreateImageViewSingleMip(uint32_t mip)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		nvrhi::TextureSubresourceSet tss;
		tss.baseMipLevel = mip;
		tss.numMipLevels = 1;
		tss.baseArraySlice = 0;
		tss.numArraySlices = 6;
		return tss;
	}

	void TextureCube::GenerateMips()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (Utils::IsBlockCompressed(m_Specification.Format))
		{
			LUX_CORE_WARN("TextureCube::GenerateMips - Skipping compute-based mip generation for block-compressed texture '{}'. "
				"Compressed mip chains must be authored by the import pipeline.", m_Specification.DebugName);
			return;
		}

		Ref<RenderCommandBuffer> renderCommandBuffer = RenderCommandBuffer::Create(1, std::format("TextureCube::GenerateMips - {}", m_Specification.DebugName));

		Ref<Shader> shader = Renderer::GetShaderLibrary()->Get("LinearSample");

		LUX_CORE_WARN("TextureCube {}: Generating mips for format {}", m_Specification.DebugName, (int)m_Specification.Format);

		ComputePassSpecification spec;
		spec.DebugName = "LinearSample";
		spec.Pipeline = Renderer::GetOrCreateMipGenPipeline(shader); // cached: built once, not per texture
		Ref<ComputePass> computePass = ComputePass::Create(spec);

		renderCommandBuffer->Begin();

		Ref<TextureCube> instance = this;
		//Renderer::Submit([renderCommandBuffer, instance]()
		//{
		//	nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
		//
		//	commandList->setTextureState(instance->m_Image->GetImageInfo().ImageHandle, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
		//	commandList->commitBarriers();
		//});

		Renderer::BeginComputePass(renderCommandBuffer, computePass);

		struct PushConstants
		{
			glm::vec2 TexelSize;
			int SourceMip;
		} pushConstants;

		ImageViewSpecification srcImageViewSpec;
		srcImageViewSpec.Image = m_Image;
		srcImageViewSpec.MipCount = 1;
		srcImageViewSpec.LayerCount = 1;
		srcImageViewSpec.Dimension = nvrhi::TextureDimension::Texture2D;

		ImageViewSpecification dstImageViewSpec = srcImageViewSpec;

		const auto mipLevels = GetMipLevelCount();
		for (uint32_t face = 0; face < 6; face++)
		{
			float targetMipWidth = static_cast<float>(m_Specification.Width);
			float targetMipHeight = static_cast<float>(m_Specification.Height);

			srcImageViewSpec.Layer = face;
			dstImageViewSpec.Layer = face;

			for (uint32_t mip = 1; mip < mipLevels; mip++)
			{
				targetMipWidth /= 2;
				targetMipHeight /= 2;

				srcImageViewSpec.Mip = mip - 1;
				dstImageViewSpec.Mip = mip;

				Ref<ImageView> srcImageView = ImageView::Create(srcImageViewSpec);
				Ref<ImageView> dstImageView = ImageView::Create(dstImageViewSpec);

				Ref<Material> material = Material::Create(shader);

				material->Set("u_InputTexture", srcImageView);
				material->Set("o_OutputTexture", dstImageView);

				pushConstants.TexelSize = { 1.0f / targetMipWidth , 1.0f / targetMipHeight };
				pushConstants.SourceMip = mip - 1;

				glm::uvec3 workGroups{ targetMipWidth / 8, targetMipHeight / 8, 1 };
				workGroups = glm::max(workGroups, { 1 });
				Renderer::DispatchCompute(renderCommandBuffer, computePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));

				Renderer::Submit([renderCommandBuffer, instance]()
					{
						nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
						commandList->commitBarriers();
					});
			}
		}

		Renderer::EndComputePass(renderCommandBuffer, computePass);

		//Renderer::Submit([renderCommandBuffer, instance]()
		//{
		//	nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
		//
		//	commandList->setTextureState(instance->m_Image->GetImageInfo().ImageHandle, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
		//	commandList->commitBarriers();
		//});

		renderCommandBuffer->End();
		renderCommandBuffer->Submit();
	}


	// Not implemented since the move to NVRHI: leaves the buffer empty. The old raw-Vulkan
	// readback was removed; TextureRuntimeSerializer therefore exports no cube texels.
	void TextureCube::CopyToHostBuffer(Buffer& buffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
	}

	// Not implemented since the move to NVRHI: does nothing.
	void TextureCube::CopyFromBuffer(const Buffer& buffer, uint32_t mips)
	{
		LUX_PROFILE_FUNCTION_AUTO;
	}

}
