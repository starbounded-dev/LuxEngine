#pragma once

#include "Lux/Renderer/Image.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Lux {

	class RenderGraph
	{
	public:
		using ResourceHandle = uint32_t;
		using ExecuteCallback = std::function<void()>;
		static constexpr ResourceHandle InvalidResource = UINT32_MAX;

		enum class PassFlags : uint32_t
		{
			None = 0,
			Graphics = BIT(0),
			Compute = BIT(1),
			Transfer = BIT(2),
			SideEffect = BIT(3),
			NeverCull = BIT(4)
		};

		struct TextureDesc
		{
			std::string Name;
			Ref<Image2D> Image;
			ImageFormat Format = ImageFormat::RGBA;
			ImageUsage Usage = ImageUsage::Attachment;
			nvrhi::TextureDimension Dimension = nvrhi::TextureDimension::Texture2D;
			uint32_t Width = 1;
			uint32_t Height = 1;
			uint32_t Mips = 1;
			uint32_t Layers = 1;
			bool Transient = true;
			bool AllowAlias = true;
		};

		struct PassDesc
		{
			std::string Name;
			std::vector<ResourceHandle> Reads;
			std::vector<ResourceHandle> Writes;
			PassFlags Flags = PassFlags::None;
			ExecuteCallback Execute;
		};

		struct ResourceLifetime
		{
			ResourceHandle Resource = InvalidResource;
			uint32_t FirstPass = UINT32_MAX;
			uint32_t LastPass = 0;
			uint32_t AliasIndex = UINT32_MAX;
		};

		struct CompileResult
		{
			std::vector<ResourceLifetime> Lifetimes;
			std::vector<uint32_t> ExecutionOrder;
			std::vector<uint32_t> CulledPasses;
			std::vector<std::string> Diagnostics;
			bool Valid = true;
		};

		void Reset();
		ResourceHandle AddTransientTexture(const TextureDesc& desc);
		uint32_t AddPass(PassDesc desc);
		uint32_t AddPass(PassDesc desc, ExecuteCallback execute);
		CompileResult Compile() const;
		CompileResult Execute() const;
		void Execute(const CompileResult& compileResult) const;
		std::vector<ResourceLifetime> BuildAliasPlan() const;

		const std::vector<TextureDesc>& GetTextures() const { return m_Textures; }
		const std::vector<PassDesc>& GetPasses() const { return m_Passes; }

		static constexpr PassFlags CombineFlags(PassFlags lhs, PassFlags rhs)
		{
			return static_cast<PassFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
		}

		static constexpr bool HasFlag(PassFlags flags, PassFlags flag)
		{
			return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
		}

	private:
		static bool AreAliasCompatible(const TextureDesc& lhs, const TextureDesc& rhs);
		static void NormalizeResourceList(std::vector<ResourceHandle>& resources);
		std::vector<ResourceLifetime> BuildResourceLifetimes(std::vector<std::string>* diagnostics = nullptr) const;

		std::vector<TextureDesc> m_Textures;
		std::vector<PassDesc> m_Passes;
	};

}
