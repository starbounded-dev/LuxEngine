// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Texture.h"

#include "Lux/Renderer/RendererAPI.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/UploadContext.h"
#include "Lux/Renderer/TextureReadback.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

#include "Lux/Asset/TextureImporter.h"

#include <atomic>

namespace Lux {

	namespace Utils {

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

		// Records a compute downsample of every mip of image after the first, each level read from the
		// one above. Level sizes match the GPU's: each side halves, rounding down, and never below 1.
		static void RecordMipDownsample(Ref<RenderCommandBuffer> commandBuffer, Ref<Image2D> image, Ref<Shader> shader)
		{
			ComputePassSpecification spec;
			spec.DebugName = "LinearSample";
			spec.Pipeline = Renderer::GetOrCreateMipGenPipeline(shader); // cached: built once, not per texture
			Ref<ComputePass> computePass = ComputePass::Create(spec);

			Renderer::Submit([commandBuffer, image]() mutable
				{
					commandBuffer->RT_TransitionTextureState(image->GetImageInfo().Tracked, AllSubresources, ResourceState::UnorderedAccess);
					commandBuffer->RT_CommitBarriers();
				});

			Renderer::BeginComputePass(commandBuffer, computePass);

			struct PushConstants
			{
				glm::vec2 TexelSize;
				int SourceMip;
			} pushConstants;

			ImageViewSpecification srcImageViewSpec;
			srcImageViewSpec.Image = image;
			srcImageViewSpec.MipCount = 1;

			ImageViewSpecification dstImageViewSpec = srcImageViewSpec;

			const ImageSpecification& imageSpec = image->GetSpecification();
			for (uint32_t mip = 1; mip < imageSpec.Mips; mip++)
			{
				const uint32_t mipWidth = glm::max(imageSpec.Width >> mip, 1u);
				const uint32_t mipHeight = glm::max(imageSpec.Height >> mip, 1u);

				dstImageViewSpec.Mip = mip;
				srcImageViewSpec.Mip = mip - 1;

				Ref<ImageView> srcImageView = ImageView::Create(srcImageViewSpec);
				Ref<ImageView> dstImageView = ImageView::Create(dstImageViewSpec);

				Ref<Material> material = Material::Create(shader);

				material->Set("u_InputTexture", srcImageView);
				material->Set("o_OutputTexture", dstImageView);

				pushConstants.TexelSize = { 1.0f / static_cast<float>(mipWidth), 1.0f / static_cast<float>(mipHeight) };
				pushConstants.SourceMip = mip - 1;

				// Rounded up: a truncated count leaves the last columns and rows of odd-sized levels unwritten.
				const glm::uvec3 workGroups{ (mipWidth + 7) / 8, (mipHeight + 7) / 8, 1 };
				Renderer::DispatchCompute(commandBuffer, computePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));

				Renderer::Submit([commandBuffer]() mutable
					{
						commandBuffer->RT_CommitBarriers();
					});
			}

			Renderer::EndComputePass(commandBuffer, computePass);

			Renderer::Submit([commandBuffer, image]() mutable
				{
					commandBuffer->RT_TransitionTextureState(image->GetImageInfo().Tracked, AllSubresources, ResourceState::ShaderResource);
					commandBuffer->RT_CommitBarriers();
				});
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
		// The same bytes, read as sRGB. Only mip 0 is copied: the source's chain was averaged on the
		// encoded bytes, which darkens it, so the sRGB texture builds its own in linear space.
		const uint32_t mipCount = texture->GetImage()->GetSpecification().Mips;

		TextureSpecification spec;
		spec.Width = texture->GetWidth();
		spec.Height = texture->GetHeight();
		spec.Format = ImageFormat::SRGBA;
		spec.GenerateMips = false; // the chain is generated below, once the image holds mip 0
		spec.SamplerWrap = texture->m_Specification.SamplerWrap;
		spec.SamplerFilter = texture->m_Specification.SamplerFilter;
		spec.MaxAnisotropy = texture->m_Specification.MaxAnisotropy;
		spec.DebugName = texture->m_Specification.DebugName + " (sRGB)";

		ImageSpecification imageSpec;
		imageSpec.DebugName = spec.DebugName;
		imageSpec.Format = ImageFormat::SRGBA;
		imageSpec.Width = spec.Width;
		imageSpec.Height = spec.Height;
		imageSpec.Mips = mipCount;
		imageSpec.Transfer = true;

		Ref<Image2D> srgbImage = Image2D::Create(imageSpec);
		srgbImage->Invalidate();

		Ref<Texture2D> srgbTexture = Ref<Texture2D>::Create(spec);
		srgbTexture->m_Image = srgbImage;

		Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, "CreateFromSRGB-CopyMip0");
		commandBuffer->Begin();

		Renderer::Submit([srgbImage, texture, commandBuffer]() mutable
			{
				const ImageInfo& source = texture->GetImage()->GetImageInfo();
				const ImageInfo& destination = srgbImage->GetImageInfo();
				if (!source.RHITexture || !destination.RHITexture)
					return;

				const TextureSubresourceRange topMip{ 0, 1, 0, 1 };
				commandBuffer->RT_RequireTextureState(source.Tracked, topMip, ResourceState::CopySource);
				commandBuffer->RT_RequireTextureState(destination.Tracked, topMip, ResourceState::CopyDest);
				nri::CommandBuffer* nriCommandBuffer = commandBuffer->RT_BeginNRITransfer();
				if (!nriCommandBuffer)
					return;

				nri::TextureRegionDesc region = {};
				region.width = static_cast<nri::Dim_t>(srgbImage->GetSpecification().Width);
				region.height = static_cast<nri::Dim_t>(srgbImage->GetSpecification().Height);
				region.depth = 1;
				region.planes = nri::PlaneBits::ALL;
				RHIDevice::API().CmdCopyTexture(*nriCommandBuffer, *destination.RHITexture, &region, *source.RHITexture, &region);
				commandBuffer->RT_EndNRISegment();
			});

		commandBuffer->End();
		commandBuffer->Submit();

		if (mipCount > 1)
			srgbTexture->GenerateMips();

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
		m_Image->Release();

		uint32_t mipCount = m_Specification.GenerateMips ? GetMipLevelCount() : 1;

		ImageSpecification& imageSpec = m_Image->GetSpecification();
		imageSpec.Format = m_Specification.Format;
		imageSpec.Width = m_Specification.Width;
		imageSpec.Height = m_Specification.Height;
		imageSpec.Mips = mipCount;
		imageSpec.Transfer = true;
		if (!m_ImageData) // TODO(Yan): better management for this, probably from texture spec
			imageSpec.Usage = ImageUsage::Storage;

		Ref<Image2D> image = m_Image.As<Image2D>();
		image->RT_Invalidate();

		auto& info = image->GetImageInfo();

		if (m_ImageData)
		{
			SetData(m_ImageData); // also generates the mip chain
		}

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
		if (m_Specification.Format == ImageFormat::SRGB)
		{
			LUX_CORE_WARN("Texture2D::GenerateMips - Skipping mip generation for 3-channel SRGB texture '{}'. Use SRGBA.", m_Specification.DebugName);
			return;
		}
		if (Utils::IsBlockCompressed(m_Specification.Format))
		{
			LUX_CORE_WARN("Texture2D::GenerateMips - Skipping compute-based mip generation for block-compressed texture '{}'. "
				"Compressed mip chains must be authored by the import pipeline.", m_Specification.DebugName);
			return;
		}

		LUX_CORE_WARN("{}: Generating mips for format {}", m_Specification.DebugName, Utils::ImageFormatToString(m_Specification.Format));

		Ref<RenderCommandBuffer> renderCommandBuffer = RenderCommandBuffer::Create(1, std::format("Texture2D::GenerateMips - {}", m_Specification.DebugName));
		renderCommandBuffer->Begin();

		if (m_Specification.Format != ImageFormat::SRGBA)
		{
			Ref<Shader> shader = Renderer::GetShaderLibrary()->Get(Utils::IsIntegerBased(m_Specification.Format) ? "LinearSampleUInt" : "LinearSample");
			Utils::RecordMipDownsample(renderCommandBuffer, m_Image, shader);
		}
		else
		{
			// sRGB images cannot be storage images, so the chain is built in an RGBA copy of mip 0, with a
			// downsample that averages in linear space, and then copied back.
			const ImageSpecification& imageSpec = m_Image->GetSpecification();
			ImageSpecification scratchSpec;
			scratchSpec.DebugName = m_Specification.DebugName + " (sRGB mip chain)";
			scratchSpec.Format = ImageFormat::RGBA;
			scratchSpec.Width = imageSpec.Width;
			scratchSpec.Height = imageSpec.Height;
			scratchSpec.Mips = imageSpec.Mips;
			scratchSpec.Transfer = true;
			Ref<Image2D> scratch = Image2D::Create(scratchSpec);
			scratch->Invalidate();

			Ref<Image2D> image = m_Image;
			Renderer::Submit([renderCommandBuffer, image, scratch]() mutable
				{
					const ImageInfo& source = image->GetImageInfo();
					const ImageInfo& destination = scratch->GetImageInfo();
					if (!source.RHITexture || !destination.RHITexture)
						return;

					const TextureSubresourceRange topMip{ 0, 1, 0, 1 };
					renderCommandBuffer->RT_RequireTextureState(source.Tracked, topMip, ResourceState::CopySource);
					renderCommandBuffer->RT_RequireTextureState(destination.Tracked, topMip, ResourceState::CopyDest);
					nri::CommandBuffer* nriCommandBuffer = renderCommandBuffer->RT_BeginNRITransfer();
					if (!nriCommandBuffer)
						return;

					nri::TextureRegionDesc region = {};
					region.width = static_cast<nri::Dim_t>(image->GetSpecification().Width);
					region.height = static_cast<nri::Dim_t>(image->GetSpecification().Height);
					region.depth = 1;
					region.planes = nri::PlaneBits::ALL;
					RHIDevice::API().CmdCopyTexture(*nriCommandBuffer, *destination.RHITexture, &region, *source.RHITexture, &region);
					renderCommandBuffer->RT_EndNRISegment();
				});

			Utils::RecordMipDownsample(renderCommandBuffer, scratch, Renderer::GetShaderLibrary()->Get("LinearSampleSRGB"));

			Renderer::Submit([renderCommandBuffer, image, scratch]() mutable
				{
					const ImageInfo& source = scratch->GetImageInfo();
					const ImageInfo& destination = image->GetImageInfo();
					if (!source.RHITexture || !destination.RHITexture)
						return;

					// Every mip, in one copy: mip 0 is the same in both, the rest are the new chain.
					renderCommandBuffer->RT_RequireTextureState(source.Tracked, AllSubresources, ResourceState::CopySource);
					renderCommandBuffer->RT_RequireTextureState(destination.Tracked, AllSubresources, ResourceState::CopyDest);
					nri::CommandBuffer* nriCommandBuffer = renderCommandBuffer->RT_BeginNRITransfer();
					if (!nriCommandBuffer)
						return;

					RHIDevice::API().CmdCopyTexture(*nriCommandBuffer, *destination.RHITexture, nullptr, *source.RHITexture, nullptr);
					renderCommandBuffer->RT_EndNRISegment();
				});
		}

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
			// Six faces of mip 0. Sized from the format: a fixed 4 bytes per texel under-sized
			// float cubes, and the per-face upload in Invalidate() then read past the copy.
			const uint64_t size = Utils::GetImageMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height) * 6;
			if (data.Size >= size)
				m_LocalStorage = Buffer::Copy(data.Data, size);
			else
				LUX_CORE_ERROR_TAG("Renderer", "TextureCube '{}': initial data is {} bytes, six faces need {}; creating it without data", m_Specification.DebugName, data.Size, size);
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
		imageSpec.Dimension = TextureDimension::TextureCube;
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
			Renderer::RecordResourceUpload([&](UploadContext& upload)
			{
				// NOTE(Yan): ONLY WORKS FOR MIP 0!
				const uint8_t* data = m_LocalStorage.As<uint8_t>();
				uint64_t stride = m_LocalStorage.Size / 6;
				for (uint32_t i = 0; i < 6; i++)
				{
					upload.WriteTexture(m_Image->GetImageInfo().Tracked, i, 0, data, Utils::GetImageMemoryRowPitch(m_Specification.Format, m_Specification.Width));
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

	TextureSubresourceRange TextureCube::CreateImageViewSingleMip(uint32_t mip)
	{
		return { mip, 1, 0, 6 };
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
		srcImageViewSpec.Dimension = TextureDimension::Texture2D;

		ImageViewSpecification dstImageViewSpec = srcImageViewSpec;

		const auto mipLevels = GetMipLevelCount();
		for (uint32_t face = 0; face < 6; face++)
		{
			srcImageViewSpec.Layer = face;
			dstImageViewSpec.Layer = face;

			for (uint32_t mip = 1; mip < mipLevels; mip++)
			{
				const uint32_t mipWidth = glm::max(m_Specification.Width >> mip, 1u);
				const uint32_t mipHeight = glm::max(m_Specification.Height >> mip, 1u);

				srcImageViewSpec.Mip = mip - 1;
				dstImageViewSpec.Mip = mip;

				Ref<ImageView> srcImageView = ImageView::Create(srcImageViewSpec);
				Ref<ImageView> dstImageView = ImageView::Create(dstImageViewSpec);

				Ref<Material> material = Material::Create(shader);

				material->Set("u_InputTexture", srcImageView);
				material->Set("o_OutputTexture", dstImageView);

				pushConstants.TexelSize = { 1.0f / static_cast<float>(mipWidth), 1.0f / static_cast<float>(mipHeight) };
				pushConstants.SourceMip = mip - 1;

				const glm::uvec3 workGroups{ (mipWidth + 7) / 8, (mipHeight + 7) / 8, 1 };
				Renderer::DispatchCompute(renderCommandBuffer, computePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));

				Renderer::Submit([renderCommandBuffer, instance]() mutable
					{
						renderCommandBuffer->RT_CommitBarriers();
					});
			}
		}

		Renderer::EndComputePass(renderCommandBuffer, computePass);

		renderCommandBuffer->End();
		renderCommandBuffer->Submit();
	}


	// Layout (shared with CopyFromBuffer and TextureRuntimeSerializer): mip-major, the six faces
	// of each mip back to back, rows tightly packed. Records, submits and waits on the cube's own
	// command list, like Image2D::CopyToHostBuffer.
	void TextureCube::CopyToHostBuffer(Buffer& buffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		buffer.Release();

		if (!m_Image)
			return;

		if (!m_CommandList)
			m_CommandList = RenderCommandBuffer::Create(1, "TextureCube");

		const uint32_t mipCount = m_Image->GetSpecification().Mips;
		std::vector<TextureSubresource> subresources;
		subresources.reserve(static_cast<size_t>(mipCount) * 6);
		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			for (uint32_t face = 0; face < 6; face++)
				subresources.push_back({ mip, face });
		}
		ReadbackTexture(*m_CommandList, m_Image->GetImageInfo().Tracked, subresources, buffer, m_Specification.DebugName);
	}

	// Inverse of CopyToHostBuffer, same layout. Mips beyond the image's own count are ignored.
	// Uploads go through the shared batch, like the constructor's initial data.
	void TextureCube::CopyFromBuffer(const Buffer& buffer, uint32_t mips)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const TrackedTexture texture = m_Image ? m_Image->GetImageInfo().Tracked : TrackedTexture{};
		if (!buffer || !texture.Handle || mips == 0)
			return;

		const ImageFormat format = m_Specification.Format;
		const uint32_t mipCount = glm::min(mips, m_Image->GetSpecification().Mips);
		const uint64_t requiredSize = Utils::GetImageMemorySize(format, m_Specification.Width, m_Specification.Height, mipCount, 6);
		if (buffer.Size < requiredSize)
		{
			LUX_CORE_ERROR_TAG("Renderer", "TextureCube '{}': buffer is {} bytes, {} mip(s) of six faces need {}", m_Specification.DebugName, buffer.Size, mipCount, requiredSize);
			return;
		}

		Renderer::RecordResourceUpload([&](UploadContext& upload)
		{
			const byte* src = static_cast<const byte*>(buffer.Data);
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				const uint32_t mipWidth = glm::max(1u, m_Specification.Width >> mip);
				const uint32_t mipHeight = glm::max(1u, m_Specification.Height >> mip);
				const uint64_t rowPitch = Utils::GetImageMemoryRowPitch(format, mipWidth);
				const uint64_t faceSize = Utils::GetImageMemorySize(format, mipWidth, mipHeight);

				for (uint32_t face = 0; face < 6; face++)
				{
					upload.WriteTexture(texture, face, mip, src, rowPitch);
					src += faceSize;
				}
			}
		});
	}

}
