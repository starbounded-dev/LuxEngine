#pragma once

#include "Lux/Renderer/Image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Lux {

	class RenderGraph
	{
	public:
		using ResourceHandle = uint32_t;
		static constexpr ResourceHandle InvalidResource = UINT32_MAX;

		struct TextureDesc
		{
			std::string Name;
			ImageFormat Format = ImageFormat::RGBA;
			uint32_t Width = 1;
			uint32_t Height = 1;
			uint32_t Mips = 1;
			bool Transient = true;
		};

		struct PassDesc
		{
			std::string Name;
			std::vector<ResourceHandle> Reads;
			std::vector<ResourceHandle> Writes;
		};

		struct ResourceLifetime
		{
			ResourceHandle Resource = InvalidResource;
			uint32_t FirstPass = UINT32_MAX;
			uint32_t LastPass = 0;
			uint32_t AliasIndex = UINT32_MAX;
		};

		void Reset();
		ResourceHandle AddTransientTexture(const TextureDesc& desc);
		uint32_t AddPass(const PassDesc& desc);
		std::vector<ResourceLifetime> BuildAliasPlan() const;

		const std::vector<TextureDesc>& GetTextures() const { return m_Textures; }
		const std::vector<PassDesc>& GetPasses() const { return m_Passes; }

	private:
		std::vector<TextureDesc> m_Textures;
		std::vector<PassDesc> m_Passes;
	};

}
