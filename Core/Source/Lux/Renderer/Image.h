#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Buffer.h"

#include "RendererResource.h"

#include "nvrhi/nvrhi.h"

#include <glm/gtc/integer.hpp>

namespace Lux {

	enum class ImageFormat
	{
		None = 0,
		RED8UN,
		RED8UI,
		RED16UI,
		RED32UI,
		RG32UI,
		RED32F,
		RG8,
		RG16F,
		RG32F,
		RGB,
		RGBA,
		RGBA16F,
		RGBA32F,

		B10R11G11UF,

		SRGB,
		SRGBA,

		BC1,
		BC1_SRGB,
		BC3,
		BC3_SRGB,
		BC5,
		BC5_SNORM,
		BC7,
		BC7_SRGB,

		DEPTH32FSTENCIL8UINT,
		DEPTH32F,
		DEPTH24STENCIL8,

		// Defaults
		// D24S8 is optional in the Vulkan spec and unsupported on Mesa RADV/ANV (AMD/Intel), which
		// only guarantee D32S8 — request that instead so depth/shadow attachments actually create
		// on those drivers.
		Depth = DEPTH32FSTENCIL8UINT,
	};

	enum class ImageUsage
	{
		None = 0,
		Texture,
		Attachment,
		Storage,
		HostRead
	};

	enum class TextureWrap
	{
		None = 0,
		Clamp,
		Repeat
	};

	enum class TextureFilter
	{
		None = 0,
		Linear,
		Nearest,
		Cubic
	};

	enum class TextureType
	{
		None = 0,
		Texture2D,
		Texture3D,
		TextureCube
	};

	struct ImageSpecification
	{
		std::string DebugName;

		nvrhi::TextureDimension Dimension = nvrhi::TextureDimension::Texture2D;
		ImageFormat Format = ImageFormat::RGBA;
		ImageUsage Usage = ImageUsage::Texture;
		bool Transfer = false; // Will it be used for transfer ops?
		uint32_t Width = 1;
		uint32_t Height = 1;
		uint32_t Depth = 1; // Only used when Dimension == Texture3D
		uint32_t Mips = 1;
		uint32_t Layers = 1;
		// MSAA sample count. 1 = no multisampling. Anything above 1 makes this a
		// multisampled image: it can be rendered into and resolved, but it cannot be
		// sampled with an ordinary sampler2D - shaders need sampler2DMS and an explicit
		// sample index. Only valid for ImageUsage::Attachment, and mips must be 1.
		uint32_t Samples = 1;
		bool CreateSampler = true;
		float MipBias = 0.0f;
	};

	struct ImageInfo
	{
		nvrhi::TextureHandle ImageHandle = nullptr;
		nvrhi::TextureSubresourceSet ImageView = nvrhi::AllSubresources;
		nvrhi::SamplerHandle Sampler = nullptr;
		nvrhi::ResourceStates State = nvrhi::ResourceStates::Unknown;
		nvrhi::TextureDimension Dimension = nvrhi::TextureDimension::Unknown;
	};

	class RenderCommandBuffer;

	class Image : public RendererResource
	{
	public:
		virtual void Resize(const glm::uvec2& size) = 0;
		virtual void Resize(const uint32_t width, const uint32_t height) = 0;
		virtual void Invalidate() = 0;
		virtual void Release() = 0;

		virtual nvrhi::TextureHandle GetHandle() const = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual glm::uvec2 GetSize() const = 0;
		virtual bool HasMips() const = 0;

		virtual float GetAspectRatio() const = 0;

		virtual ImageSpecification& GetSpecification() = 0;
		virtual const ImageSpecification& GetSpecification() const = 0;

		virtual Buffer GetBuffer() const = 0;
		virtual Buffer& GetBuffer() = 0;

		virtual uint64_t GetGPUMemoryUsage() const = 0;

		virtual void CreatePerLayerImageViews() = 0;

		virtual uint64_t GetHash() const = 0;

		virtual void SetData(Buffer buffer) = 0;
		virtual void CopyToHostBuffer(Buffer& buffer) const = 0;

		// TODO: usage (eg. shader read)
	public:
		virtual ~Image() = default;
	};

	class Image2D : public Image
	{
	public:
		static Ref<Image2D> Create(const ImageSpecification& specification) { return Ref<Image2D>::Create(specification); }

		bool IsValid() const { return m_TransientAliasSource ? m_TransientAliasSource->IsValid() : m_Info.ImageHandle != nullptr; }
		bool IsTransientAlias() const { return m_TransientAliasSource != nullptr; }
		Ref<Image2D> GetTransientAliasSource() const { return m_TransientAliasSource; }
		void SetTransientAliasSource(Ref<Image2D> source);
		void ClearTransientAliasSource();

		virtual void Resize(const glm::uvec2& size) override
		{
			Resize(size.x, size.y);
		}

		virtual void Resize(const uint32_t width, const uint32_t height) override
		{
			m_Specification.Width = width;
			m_Specification.Height = height;
			Invalidate();
		}
		virtual void Invalidate() override;
		virtual void Release() override;

		virtual nvrhi::TextureHandle GetHandle() const override { return m_TransientAliasSource ? m_TransientAliasSource->GetHandle() : m_Info.ImageHandle; }
		virtual uint32_t GetWidth() const override { return m_Specification.Width; }
		virtual uint32_t GetHeight() const override { return m_Specification.Height; }
		virtual glm::uvec2 GetSize() const override { return { m_Specification.Width, m_Specification.Height }; }
		virtual bool HasMips() const override { return m_Specification.Mips > 1; }

		virtual float GetAspectRatio() const override { return (float)m_Specification.Width / (float)m_Specification.Height; }

		int GetClosestMipLevel(uint32_t width, uint32_t height) const;
		std::pair<uint32_t, uint32_t> GetMipLevelSize(int mipLevel) const;

		virtual ImageSpecification& GetSpecification() override { return m_Specification; }
		virtual const ImageSpecification& GetSpecification() const override { return m_Specification; }

		void RT_Invalidate();

		virtual void CreatePerLayerImageViews() override;
		void RT_CreatePerLayerImageViews();
		void RT_CreatePerSpecificLayerImageViews(const std::vector<uint32_t>& layerIndices);

		virtual nvrhi::TextureSubresourceSet GetLayerImageView(uint32_t layer)
		{
			LUX_CORE_ASSERT(layer < m_PerLayerImageViews.size());
			return m_PerLayerImageViews[layer];
		}

		nvrhi::TextureSubresourceSet GetMipImageView(uint32_t mip);

		ImageInfo& GetImageInfo() { return m_TransientAliasSource ? m_TransientAliasSource->GetImageInfo() : m_Info; }
		const ImageInfo& GetImageInfo() const { return m_TransientAliasSource ? m_TransientAliasSource->GetImageInfo() : m_Info; }

		virtual ResourceDescriptorInfo GetDescriptorInfo() const override { return m_TransientAliasSource ? m_TransientAliasSource->GetDescriptorInfo() : (ResourceDescriptorInfo)&m_Info; }

		virtual Buffer GetBuffer() const override { return m_ImageData; }
		virtual Buffer& GetBuffer() override { return m_ImageData; }

		virtual uint64_t GetGPUMemoryUsage() const override { return m_TransientAliasSource ? 0 : m_GPUAllocationSize; }

		virtual uint64_t GetHash() const override { return (uint64_t)GetHandle().Get(); }

		// Debug
		static const std::map<nvrhi::ITexture*, WeakRef<Image2D>>& GetImageRefs();

		virtual void SetData(Buffer buffer) override;
		virtual void CopyToHostBuffer(Buffer& buffer) const override;
	public:
		Image2D(const ImageSpecification& specification);

		virtual ~Image2D();
	private:
		ImageSpecification m_Specification;

		Buffer m_ImageData;

		ImageInfo m_Info;
		uint64_t m_GPUAllocationSize = 0;
		Ref<Image2D> m_TransientAliasSource;

		mutable Ref<RenderCommandBuffer> m_CommandList;

		std::vector<nvrhi::TextureSubresourceSet> m_PerLayerImageViews;
		std::map<uint32_t, nvrhi::TextureSubresourceSet> m_PerMipImageViews;
	};

	namespace Utils {

		inline nvrhi::Format NVRHIFormat(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::RED8UN:               return nvrhi::Format::R8_UNORM;
			case ImageFormat::RED8UI:               return nvrhi::Format::R8_UINT;
			case ImageFormat::RED16UI:              return nvrhi::Format::R16_UINT;
			case ImageFormat::RED32UI:              return nvrhi::Format::R32_UINT;
			case ImageFormat::RG32UI:               return nvrhi::Format::RG32_UINT;
			case ImageFormat::RED32F:               return nvrhi::Format::R32_FLOAT;
			case ImageFormat::RG8:                  return nvrhi::Format::RG8_UNORM;
			case ImageFormat::RG16F:                return nvrhi::Format::RG16_FLOAT;
			case ImageFormat::RG32F:                return nvrhi::Format::RG32_FLOAT;
			case ImageFormat::RGBA:                 return nvrhi::Format::RGBA8_UNORM;
			case ImageFormat::SRGBA:                return nvrhi::Format::SRGBA8_UNORM;
			case ImageFormat::RGBA16F:              return nvrhi::Format::RGBA16_FLOAT;
			case ImageFormat::RGBA32F:              return nvrhi::Format::RGBA32_FLOAT;
			case ImageFormat::B10R11G11UF:          return nvrhi::Format::R11G11B10_FLOAT;
			case ImageFormat::BC1:                  return nvrhi::Format::BC1_UNORM;
			case ImageFormat::BC1_SRGB:             return nvrhi::Format::BC1_UNORM_SRGB;
			case ImageFormat::BC3:                  return nvrhi::Format::BC3_UNORM;
			case ImageFormat::BC3_SRGB:             return nvrhi::Format::BC3_UNORM_SRGB;
			case ImageFormat::BC5:                  return nvrhi::Format::BC5_UNORM;
			case ImageFormat::BC5_SNORM:            return nvrhi::Format::BC5_SNORM;
			case ImageFormat::BC7:                  return nvrhi::Format::BC7_UNORM;
			case ImageFormat::BC7_SRGB:             return nvrhi::Format::BC7_UNORM_SRGB;
			case ImageFormat::DEPTH32FSTENCIL8UINT: return nvrhi::Format::D32S8;
			case ImageFormat::DEPTH32F:             return nvrhi::Format::D32;
			case ImageFormat::DEPTH24STENCIL8:      return nvrhi::Format::D24S8;
			}

			LUX_CORE_ASSERT(false);
			return nvrhi::Format::UNKNOWN;
		}

		inline uint32_t GetImageFormatBPP(ImageFormat format);

		inline bool IsBlockCompressed(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::BC1:
			case ImageFormat::BC1_SRGB:
			case ImageFormat::BC3:
			case ImageFormat::BC3_SRGB:
			case ImageFormat::BC5:
			case ImageFormat::BC5_SNORM:
			case ImageFormat::BC7:
			case ImageFormat::BC7_SRGB:
				return true;
			default:
				return false;
			}
		}

		inline uint32_t GetImageFormatBlockSize(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::BC1:
			case ImageFormat::BC1_SRGB:
				return 8;
			case ImageFormat::BC3:
			case ImageFormat::BC3_SRGB:
			case ImageFormat::BC5:
			case ImageFormat::BC5_SNORM:
			case ImageFormat::BC7:
			case ImageFormat::BC7_SRGB:
				return 16;
			default:
				return GetImageFormatBPP(format);
			}
		}

		inline uint32_t GetImageFormatBPP(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::RED8UN:  return 1;
			case ImageFormat::RED8UI:  return 1;
			case ImageFormat::RED16UI: return 2;
			case ImageFormat::RED32UI: return 4;
			case ImageFormat::RG32UI: return 8;
			case ImageFormat::RED32F:  return 4;
			case ImageFormat::RG8:     return 2;
			case ImageFormat::RG16F:   return 2 * 2;
			case ImageFormat::RG32F:   return 4 * 2;
			case ImageFormat::RGB:
			case ImageFormat::SRGB:    return 3;
			case ImageFormat::RGBA:    return 4;
			case ImageFormat::SRGBA:   return 4;
			case ImageFormat::RGBA16F: return 2 * 4;
			case ImageFormat::RGBA32F: return 4 * 4;
			case ImageFormat::B10R11G11UF: return 4;
			case ImageFormat::DEPTH24STENCIL8: return 4;
			case ImageFormat::DEPTH32F: return 4;
			case ImageFormat::DEPTH32FSTENCIL8UINT: return 8;
			case ImageFormat::BC1:
			case ImageFormat::BC1_SRGB:
			case ImageFormat::BC3:
			case ImageFormat::BC3_SRGB:
			case ImageFormat::BC5:
			case ImageFormat::BC5_SNORM:
			case ImageFormat::BC7:
			case ImageFormat::BC7_SRGB:
				return 0;
			}
			LUX_CORE_ASSERT(false);
			return 0;
		}

		inline bool IsIntegerBased(const ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::RED16UI:
			case ImageFormat::RED32UI:
			case ImageFormat::RG32UI:
			case ImageFormat::RED8UI:
			case ImageFormat::DEPTH32FSTENCIL8UINT:
				return true;
			case ImageFormat::DEPTH32F:
			case ImageFormat::RED8UN:
			case ImageFormat::RGBA32F:
			case ImageFormat::B10R11G11UF:
			case ImageFormat::RG16F:
			case ImageFormat::RG32F:
			case ImageFormat::RED32F:
			case ImageFormat::RG8:
			case ImageFormat::RGBA:
			case ImageFormat::RGBA16F:
			case ImageFormat::RGB:
			case ImageFormat::SRGB:
			case ImageFormat::SRGBA:
			case ImageFormat::BC1:
			case ImageFormat::BC1_SRGB:
			case ImageFormat::BC3:
			case ImageFormat::BC3_SRGB:
			case ImageFormat::BC5:
			case ImageFormat::BC5_SNORM:
			case ImageFormat::BC7:
			case ImageFormat::BC7_SRGB:
			case ImageFormat::DEPTH24STENCIL8:
				return false;
			}
			LUX_CORE_ASSERT(false);
			return false;
		}

		inline uint32_t CalculateMipCount(uint32_t width, uint32_t height)
		{
			return (uint32_t)glm::floor(glm::log2(glm::min(width, height))) + 1;
		}

		inline uint32_t GetImageMemorySize(ImageFormat format, uint32_t width, uint32_t height)
		{
			if (IsBlockCompressed(format))
			{
				const uint32_t blocksWide = glm::max(1u, (width + 3u) / 4u);
				const uint32_t blocksHigh = glm::max(1u, (height + 3u) / 4u);
				return blocksWide * blocksHigh * GetImageFormatBlockSize(format);
			}
			return width * height * GetImageFormatBPP(format);
		}

		inline uint64_t GetImageMemorySize(ImageFormat format, uint32_t width, uint32_t height, uint32_t mips, uint32_t layers)
		{
			uint64_t size = 0;
			const uint32_t mipCount = glm::max(1u, mips);
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				const uint32_t mipWidth = glm::max(1u, width >> mip);
				const uint32_t mipHeight = glm::max(1u, height >> mip);
				size += GetImageMemorySize(format, mipWidth, mipHeight);
			}
			return size * glm::max(1u, layers);
		}

		inline uint32_t GetImageMemoryRowPitch(ImageFormat format, uint32_t width)
		{
			if (IsBlockCompressed(format))
				return glm::max(1u, (width + 3u) / 4u) * GetImageFormatBlockSize(format);
			return width * GetImageFormatBPP(format);
		}

		inline bool IsDepthFormat(ImageFormat format)
		{
			if (format == ImageFormat::DEPTH24STENCIL8 || format == ImageFormat::DEPTH32F || format == ImageFormat::DEPTH32FSTENCIL8UINT)
				return true;

			return false;
		}

		inline std::string_view ImageFormatToString(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::None: return "None";
			case ImageFormat::RED8UN: return "RED8UN";
			case ImageFormat::RED8UI: return "RED8UI";
			case ImageFormat::RED16UI: return "RED16UI";
			case ImageFormat::RED32UI: return "RED32UI";
			case ImageFormat::RG32UI: return "RG32UI";
			case ImageFormat::RED32F: return "RED32F";
			case ImageFormat::RG8: return "RG8";
			case ImageFormat::RG16F: return "RG16F";
			case ImageFormat::RG32F: return "RG32F";
			case ImageFormat::RGB: return "RGB";
			case ImageFormat::RGBA: return "RGBA";
			case ImageFormat::RGBA16F: return "RGBA16F";
			case ImageFormat::RGBA32F: return "RGBA32F";
			case ImageFormat::B10R11G11UF: return "B10R11G11UF";
			case ImageFormat::SRGB: return "SRGB";
			case ImageFormat::SRGBA: return "SRGBA";
			case ImageFormat::BC1: return "BC1";
			case ImageFormat::BC1_SRGB: return "BC1_SRGB";
			case ImageFormat::BC3: return "BC3";
			case ImageFormat::BC3_SRGB: return "BC3_SRGB";
			case ImageFormat::BC5: return "BC5";
			case ImageFormat::BC5_SNORM: return "BC5_SNORM";
			case ImageFormat::BC7: return "BC7";
			case ImageFormat::BC7_SRGB: return "BC7_SRGB";
			case ImageFormat::DEPTH32FSTENCIL8UINT: return "DEPTH32FSTENCIL8UINT";
			case ImageFormat::DEPTH32F: return "DEPTH32F";
			case ImageFormat::DEPTH24STENCIL8: return "DEPTH24STENCIL8";
			}

			return "<Unknown>";
		}

	}

	struct ImageViewSpecification
	{
		Ref<Image2D> Image;
		uint32_t Mip = 0;
		uint32_t MipCount = 0; // 0 means all
		uint32_t Layer = 0;
		uint32_t LayerCount = 0; // 0 means all

		// Unknown will take dimension from Texture
		nvrhi::TextureDimension Dimension = nvrhi::TextureDimension::Unknown;

		std::string DebugName;
	};

	class ImageView : public RendererResource
	{
	public:
		static Ref<ImageView> Create(const ImageViewSpecification& specification) { return Ref<ImageView>::Create(specification); }

		void Invalidate();
		void RT_Invalidate();

		const nvrhi::TextureSubresourceSet& GetImageView() const { return m_TextureSubresourceSet; }
		virtual ResourceDescriptorInfo GetDescriptorInfo() const override { return (ResourceDescriptorInfo)&m_ImageInfo; }
	public:
		ImageView(const ImageViewSpecification& specification);

		virtual ~ImageView() = default;
	private:
		ImageViewSpecification m_Specification;
		ImageInfo m_ImageInfo;

		nvrhi::TextureSubresourceSet m_TextureSubresourceSet;
	};

	struct SamplerSpecification
	{
		float MipBias = 0.0f;
		nvrhi::SamplerAddressMode AddressMode = nvrhi::SamplerAddressMode::Clamp;
		float MaxAnisotropy = 1.0f;
		bool MinFilter = true;
		bool MagFilter = true;
		bool MipFilter = true;
	};

	class Sampler : public RendererResource
	{
	public:
		static Ref<Sampler> Create(const SamplerSpecification& specification = SamplerSpecification()) { return Ref<Sampler>::Create(specification); }

		void Invalidate();

		nvrhi::SamplerHandle GetHandle() const { return m_Handle; }
		virtual ResourceDescriptorInfo GetDescriptorInfo() const override { return (ResourceDescriptorInfo)this; }
	public:
		Sampler(const SamplerSpecification& specification);

		virtual ~Sampler() = default;
	private:
		SamplerSpecification m_Specification;

		nvrhi::SamplerHandle m_Handle;
	};

}
