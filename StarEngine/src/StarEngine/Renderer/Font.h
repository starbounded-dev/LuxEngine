#pragma once

#include <filesystem>

#include "StarEngine/Core/Base.h"
#include "StarEngine/Renderer/Texture.h"

namespace StarEngine {

	struct MSDFData;

	class Font : public Asset
	{
	public:
		Font(const std::filesystem::path& font);
		~Font();

		const MSDFData* GetMSDFData() const { return m_Data; }
		Ref<Texture2D> GetAtlasTexture() const { return m_AtlasTexture; }

		static Ref<Font> GetDefaultFont();
	private:
		MSDFData* m_Data;
		Ref<Texture2D> m_AtlasTexture;

		inline static Ref<Font> s_DefaultFont;
	};


}
