#pragma once

#include "StarEngine/Core/Base.h"
#include "StarEngine/Core/Buffer.h"
#include "StarEngine/Asset/Asset.h"
#include "StarEngine/Renderer/Image.h"

#include "nvrhi/nvrhi.h"

#include <filesystem>

namespace StarEngine {

	struct TextureSpecification
	{
		ImageFormat Format = ImageFormat::RGBA;
		uint32_t Width = 1;
		uint32_t Height = 1;
		TextureWrap SamplerWrap = TextureWrap::Repeat;
		TextureFilter SamplerFilter = TextureFilter::Linear;
		
		bool GenerateMips = true;
		bool Storage = false;
		bool StoreLocally = false;

		std::string DebugName;
	};

	class Texture : public RendererResource
	{
	public:
		virtual ~Texture() {}

		virtual nvrhi::TextureHandle GetHandle() const = 0;

		virtual ImageFormat GetFormat() const = 0;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual glm::uvec2 GetSize() const = 0;

		virtual uint32_t GetMipLevelCount() const = 0;
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const = 0;

		virtual uint64_t GetHash() const = 0;

		virtual TextureType GetType() const = 0;
	};

	class Texture2D : public Texture
	{
	public:
		static Ref<Texture2D> Create(const TextureSpecification& specification) { return Ref<Texture2D>::Create(specification); }
		static Ref<Texture2D> Create(const TextureSpecification& specification, const std::filesystem::path& filepath) { return Ref<Texture2D>::Create(specification, filepath); }
		static Ref<Texture2D> Create(const TextureSpecification& specification, Buffer imageData) { return Ref<Texture2D>::Create(specification, imageData); }

		virtual ResourceDescriptorInfo GetDescriptorInfo() const override { return m_Image->GetDescriptorInfo(); }

		// reinterpret the given texture's data as if it was sRGB
		static Ref<Texture2D> CreateFromSRGB(Ref<Texture2D> texture);

		void CreateFromFile(const TextureSpecification& specification, const std::filesystem::path& filepath);
		void CreateFromBuffer(const TextureSpecification& specification, Buffer data = Buffer());
		
		void ReplaceFromFile(const TextureSpecification& specification, const std::filesystem::path& filepath);

		void Invalidate();

		void Resize(const glm::uvec2& size);
		void Resize(const uint32_t width, const uint32_t height);

		virtual nvrhi::TextureHandle GetHandle() const override { return m_Image->GetHandle(); }
		virtual ImageFormat GetFormat() const override { return m_Specification.Format; }
		virtual uint32_t GetWidth() const override { return m_Specification.Width; }
		virtual uint32_t GetHeight() const override { return m_Specification.Height; }
		virtual glm::uvec2 GetSize() const override { return { m_Specification.Width, m_Specification.Height }; }

		uint64_t GetEstimatedSize() const
		{
			// rough CPU-side estimate (good enough for UI)
			return (uint64_t)m_Specification.Width * (uint64_t)m_Specification.Height * 4ull;
		}


		Ref<Image2D> GetImage() const { return m_Image; }

		void Lock();
		void Unlock();

		Buffer GetWriteableBuffer();

		virtual TextureType GetType() const override { return TextureType::Texture2D; }

		bool Loaded() const { return m_Image && m_Image->IsValid(); }
		const std::filesystem::path& GetPath() const;
		uint32_t GetMipLevelCount() const;
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const;

		void GenerateMips();
		void CopyToHostBuffer(Buffer& buffer);

		void SetData(Buffer buffer);

		static AssetType GetStaticType() { return AssetType::Texture; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

		virtual uint64_t GetHash() const { return (uint64_t)m_Image->GetHandle().Get(); }
	public:
		Texture2D(const TextureSpecification& specification, const std::filesystem::path& filepath);
		Texture2D(const TextureSpecification& specification, Buffer data = Buffer());

		virtual ~Texture2D();
	private:
		TextureSpecification m_Specification;
		std::filesystem::path m_Path;

		Buffer m_ImageData;

		Ref<Image2D> m_Image;
	};
#if 0
	class TextureCube : public Texture
	{
	public:
		static Ref<TextureCube> Create(const TextureSpecification& specification, Buffer imageData = Buffer()) { return Ref<TextureCube>::Create(specification, imageData); }
		virtual TextureType GetType() const override { return TextureType::TextureCube; }

		virtual ResourceDescriptorInfo GetDescriptorInfo() const override { return m_Image->GetDescriptorInfo(); }

		static AssetType GetStaticType() { return AssetType::EnvMap; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }
		
		void Release();

		virtual nvrhi::TextureHandle GetHandle() const override { return m_Image->GetHandle(); }
		virtual ImageFormat GetFormat() const override { return m_Specification.Format; }

		virtual uint32_t GetWidth() const override { return m_Specification.Width; }
		virtual uint32_t GetHeight() const override { return m_Specification.Height; }
		virtual glm::uvec2 GetSize() const override { return { m_Specification.Width, m_Specification.Height }; }

		virtual uint32_t GetMipLevelCount() const override;
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const override;

		virtual uint64_t GetHash() const override { return (uint64_t)GetHandle().Get(); }

		Ref<Image2D> GetImage() const { return m_Image; }

		void GenerateMips();

		void CopyToHostBuffer(Buffer& buffer);
		void CopyFromBuffer(const Buffer& buffer, uint32_t mips);

		nvrhi::TextureSubresourceSet CreateImageViewSingleMip(uint32_t mip);
	public:
		TextureCube(const TextureSpecification& specification, Buffer data);
		
		virtual ~TextureCube();
	private:
		void Invalidate();
	private:
		TextureSpecification m_Specification;

		Ref<Image2D> m_Image;

		bool m_MipsGenerated = false;

		Buffer m_LocalStorage;
		uint64_t m_GPUAllocationSize = 0;

		Ref<RenderCommandBuffer> m_CommandList;
	};
#endif
}
