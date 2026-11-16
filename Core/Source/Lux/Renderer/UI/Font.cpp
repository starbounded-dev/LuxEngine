// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Font.h"

#include "MSDFData.h"

#include "Lux/Scene/Components.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/FileSystem.h"

#include <array>
#include <format>

namespace Lux {

	using namespace msdf_atlas;

	// Bump when atlas generation or the cache file layout changes; older cache files then regenerate.
	static constexpr uint32_t k_FontAtlasCacheVersion = 1;
	static constexpr std::array<char, 4> k_FontAtlasCacheMagic = { 'L', 'X', 'F', 'A' };

	struct FontInput {
		Buffer fontData;
		GlyphIdentifierType glyphIdentifierType;
		const char* charsetFilename;
		double fontScale;
		const char* fontName;
	};

	struct Configuration {
		ImageType imageType;
		msdf_atlas::ImageFormat imageFormat;
		YDirection yDirection;
		int width, height;
		double emSize;
		double pxRange;
		double angleThreshold;
		double miterLimit;
		void (*edgeColoring)(msdfgen::Shape&, double, unsigned long long);
		bool expensiveColoring;
		unsigned long long coloringSeed;
		GeneratorAttributes generatorAttributes;
	};

#define DEFAULT_ANGLE_THRESHOLD 3.0
#define DEFAULT_MITER_LIMIT 1.0
#define LCG_MULTIPLIER 6364136223846793005ull
#define LCG_INCREMENT 1442695040888963407ull
#define THREADS 8

	namespace Utils {

		static std::filesystem::path GetCacheDirectory()
		{
			//return Project::GetCacheDirectory() / "FontAtlases";
			return "Resources/Cache/FontAtlases";
		}

		static void CreateCacheDirectoryIfNeeded()
		{
			std::filesystem::path cacheDirectory = GetCacheDirectory();
			if (!std::filesystem::exists(cacheDirectory))
				std::filesystem::create_directories(cacheDirectory);
		}
	}

	struct AtlasHeader
	{
		std::array<char, 4> Magic = k_FontAtlasCacheMagic;
		uint32_t Version = k_FontAtlasCacheVersion;
		uint64_t SourceKey = 0;
		uint32_t Width = 0, Height = 0;
	};

	// Identifies what an atlas is generated from: the font's bytes and every setting that changes its
	// pixels. A font replaced under the same name, or different settings, never reads a stale atlas.
	static uint64_t GetFontAtlasCacheKey(Buffer fontData, const Configuration& config)
	{
		uint64_t hash = 14695981039346656037ull; // FNV-1a
		auto mix = [&hash](const void* data, size_t size)
		{
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
		};
		mix(fontData.Data, fontData.Size);
		mix(&config.imageType, sizeof(config.imageType));
		mix(&config.width, sizeof(config.width));
		mix(&config.height, sizeof(config.height));
		mix(&config.emSize, sizeof(config.emSize));
		mix(&config.pxRange, sizeof(config.pxRange));
		mix(&config.angleThreshold, sizeof(config.angleThreshold));
		mix(&config.miterLimit, sizeof(config.miterLimit));
		mix(&config.coloringSeed, sizeof(config.coloringSeed));
		return hash;
	}

	static std::filesystem::path GetFontAtlasCachePath(const std::string& fontName, double emSize, uint64_t key)
	{
		return Utils::GetCacheDirectory() / std::format("{}-{}-{:016x}.lfa", fontName, emSize, key);
	}

	// Reads the atlas cached for `key`. A missing file is a miss; a file that is not exactly the atlas
	// `config` describes (truncated, from another font or version) is a miss too, and is regenerated.
	static bool TryReadFontAtlasFromCache(const std::filesystem::path& filepath, uint64_t key, const Configuration& config, Buffer& storageBuffer)
	{
		if (!FileSystem::Exists(filepath))
			return false;

		storageBuffer = FileSystem::ReadBytes(filepath);
		const uint64_t pixelBytes = static_cast<uint64_t>(config.width) * static_cast<uint64_t>(config.height) * sizeof(float) * 4;
		bool valid = storageBuffer.Size == sizeof(AtlasHeader) + pixelBytes;
		if (valid)
		{
			const AtlasHeader& header = *storageBuffer.As<AtlasHeader>();
			valid = header.Magic == k_FontAtlasCacheMagic && header.Version == k_FontAtlasCacheVersion && header.SourceKey == key
				&& header.Width == static_cast<uint32_t>(config.width) && header.Height == static_cast<uint32_t>(config.height);
		}

		if (!valid)
		{
			LUX_CORE_WARN_TAG("Renderer", "Font atlas cache '{}' does not match its font; regenerating it", filepath.string());
			storageBuffer.Release();
		}
		return valid;
	}

	// Writes through a temporary file, so an interrupted write never leaves a cache file that reads as valid.
	static void CacheFontAtlas(const std::filesystem::path& filepath, const AtlasHeader& header, const void* pixels)
	{
		Utils::CreateCacheDirectoryIfNeeded();

		const std::filesystem::path temporary = filepath.string() + ".tmp";
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			stream.write(reinterpret_cast<const char*>(&header), sizeof(AtlasHeader));
			stream.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(header.Width) * header.Height * sizeof(float) * 4);
			stream.close();
			if (!stream)
			{
				LUX_CORE_ERROR_TAG("Renderer", "Failed to cache font atlas to {0}", temporary.string());
				std::error_code error;
				std::filesystem::remove(temporary, error);
				return;
			}
		}

		FileSystem::ReplaceFileAtomically(temporary, filepath);
	}

	// Removes this font's atlases from earlier font files or settings (and the unversioned pre-key
	// `.hfa` file), so editing a font does not accumulate stale atlases.
	static void RemoveSupersededFontAtlases(const std::string& fontName, double emSize, const std::filesystem::path& current)
	{
		std::error_code error;
		std::filesystem::remove(Utils::GetCacheDirectory() / std::format("{}-{}.hfa", fontName, emSize), error);

		const std::string prefix = std::format("{}-{}-", fontName, emSize);
		for (const auto& entry : std::filesystem::directory_iterator(Utils::GetCacheDirectory(), error))
		{
			const std::string filename = entry.path().filename().string();
			if (entry.path() != current && entry.path().extension() == ".lfa" && filename.starts_with(prefix))
				std::filesystem::remove(entry.path(), error);
		}
	}

	template <typename T, typename S, int N, GeneratorFunction<S, N> GEN_FN>
	static Ref<Texture2D> CreateAndCacheAtlas(const std::filesystem::path& cachePath, uint64_t cacheKey, const std::vector<GlyphGeometry>& glyphs, const FontGeometry& fontGeometry, const Configuration& config)
	{
		ImmediateAtlasGenerator<S, N, GEN_FN, BitmapAtlasStorage<T, N>> generator(config.width, config.height);
		generator.setAttributes(config.generatorAttributes);
		generator.setThreadCount(THREADS);
		generator.generate(glyphs.data(), (int)glyphs.size());

		msdfgen::BitmapConstRef<T, N> bitmap = (msdfgen::BitmapConstRef<T, N>) generator.atlasStorage();

		AtlasHeader header;
		header.SourceKey = cacheKey;
		header.Width = bitmap.width;
		header.Height = bitmap.height;
		CacheFontAtlas(cachePath, header, bitmap.pixels);

		TextureSpecification spec;
		spec.Format = ImageFormat::RGBA32F;
		spec.Width = header.Width;
		spec.Height = header.Height;
		spec.GenerateMips = false;
		spec.SamplerWrap = TextureWrap::Clamp;
		spec.DebugName = "FontAtlas";
		Ref<Texture2D> texture = Texture2D::Create(spec, Buffer(bitmap.pixels, 0));
		return texture;
	}

	static Ref<Texture2D> CreateCachedAtlas(AtlasHeader header, const void* pixels)
	{
		TextureSpecification spec;
		spec.Format = ImageFormat::RGBA32F;
		spec.Width = header.Width;
		spec.Height = header.Height;
		spec.GenerateMips = false;
		spec.SamplerWrap = TextureWrap::Clamp;
		spec.DebugName = "FontAtlas";
		Ref<Texture2D> texture = Texture2D::Create(spec, Buffer(pixels, 0));
		return texture;
	}

	Font::Font(const std::filesystem::path& filepath)
		: m_MSDFData(new MSDFData())
	{
		m_Name = filepath.stem().string();

		Buffer buffer = FileSystem::ReadBytes(filepath);
		CreateAtlas(buffer);
		buffer.Release();
	}

	Font::Font(const std::string& name, Buffer buffer)
		: m_Name(name), m_MSDFData(new MSDFData())
	{
		CreateAtlas(buffer);
	}

	Font::~Font()
	{
		delete m_MSDFData;
	}

	void Font::CreateAtlas(Buffer buffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		int result = 0;
		FontInput fontInput = { };
		Configuration config = { };
		fontInput.fontData = buffer;
		fontInput.glyphIdentifierType = GlyphIdentifierType::UNICODE_CODEPOINT;
		fontInput.fontScale = -1;
		config.imageType = ImageType::MSDF;
		config.imageFormat = msdf_atlas::ImageFormat::BINARY_FLOAT;
		config.yDirection = YDirection::BOTTOM_UP;
		config.edgeColoring = msdfgen::edgeColoringInkTrap;
		const char* imageFormatName = nullptr;
		int fixedWidth = -1, fixedHeight = -1;
		config.generatorAttributes.config.overlapSupport = true;
		config.generatorAttributes.scanlinePass = true;
		double minEmSize = 0;
		double rangeValue = 2.0;
		TightAtlasPacker::DimensionsConstraint atlasSizeConstraint = TightAtlasPacker::DimensionsConstraint::MULTIPLE_OF_FOUR_SQUARE;
		config.angleThreshold = DEFAULT_ANGLE_THRESHOLD;
		config.miterLimit = DEFAULT_MITER_LIMIT;
		config.imageType = ImageType::MTSDF;

		config.emSize = 40;

		// Load fonts
		bool anyCodepointsAvailable = false;
		class FontHolder
		{
			msdfgen::FreetypeHandle* ft;
			msdfgen::FontHandle* font;
		public:
			FontHolder() : ft(msdfgen::initializeFreetype()), font(nullptr) {}
			~FontHolder()
			{
				if (ft)
				{
					if (font)
						msdfgen::destroyFont(font);
					msdfgen::deinitializeFreetype(ft);
				}
			}
			bool load(Buffer buffer)
			{
				if (ft && buffer)
				{
					if (font)
						msdfgen::destroyFont(font);
					if ((font = msdfgen::loadFontData(ft, buffer.As<const msdfgen::byte>(), int(buffer.Size))))
						return true;
				}
				return false;
			}
			operator msdfgen::FontHandle* () const
			{
				return font;
			}
		} font;

		bool success = font.load(fontInput.fontData);
		LUX_CORE_ASSERT(success);

		if (fontInput.fontScale <= 0)
			fontInput.fontScale = 1;

		// Load character set
		fontInput.glyphIdentifierType = GlyphIdentifierType::UNICODE_CODEPOINT;
		Charset charset;

		// From ImGui
		static const uint32_t charsetRanges[] =
		{
			0x0020, 0x00FF, // Basic Latin + Latin Supplement
			0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
			0x2DE0, 0x2DFF, // Cyrillic Extended-A
			0xA640, 0xA69F, // Cyrillic Extended-B
			0,
		};

		for (int range = 0; range < 8; range += 2)
		{
			for (uint32_t c = charsetRanges[range]; c <= charsetRanges[range + 1]; c++)
				charset.add(c);
		}

		// Load glyphs
		m_MSDFData->FontGeometry = FontGeometry(&m_MSDFData->Glyphs);
		int glyphsLoaded = -1;
		switch (fontInput.glyphIdentifierType)
		{
		case GlyphIdentifierType::GLYPH_INDEX:
			glyphsLoaded = m_MSDFData->FontGeometry.loadGlyphset(font, fontInput.fontScale, charset);
			break;
		case GlyphIdentifierType::UNICODE_CODEPOINT:
			glyphsLoaded = m_MSDFData->FontGeometry.loadCharset(font, fontInput.fontScale, charset);
			anyCodepointsAvailable |= glyphsLoaded > 0;
			break;
		}

		LUX_CORE_ASSERT(glyphsLoaded >= 0);
		LUX_CORE_TRACE_TAG("Renderer", "Loaded geometry of {} out of {} glyphs", glyphsLoaded, (int)charset.size());
		// List missing glyphs
		if (glyphsLoaded < (int)charset.size())
		{
			LUX_CORE_WARN_TAG("Renderer", "Font {} is missing {} {}", m_Name, (int)charset.size() - glyphsLoaded, fontInput.glyphIdentifierType == GlyphIdentifierType::UNICODE_CODEPOINT ? "codepoints" : "glyphs");
		}

		if (fontInput.fontName)
			m_MSDFData->FontGeometry.setName(fontInput.fontName);

		// NOTE(Yan): we still need to "pack" the font to determine atlas metadata, though this could also be cached.
			//            The most intensive part is the actual atlas generation, which is what we do cache - it takes
			//            around 96% of total time spent in this Font constructor.

			// Determine final atlas dimensions, scale and range, pack glyphs
		double pxRange = rangeValue;
		bool fixedDimensions = fixedWidth >= 0 && fixedHeight >= 0;
		bool fixedScale = config.emSize > 0;
		TightAtlasPacker atlasPacker;
		if (fixedDimensions)
			atlasPacker.setDimensions(fixedWidth, fixedHeight);
		else
			atlasPacker.setDimensionsConstraint(atlasSizeConstraint);
		atlasPacker.setPadding(config.imageType == ImageType::MSDF || config.imageType == ImageType::MTSDF ? 0 : -1);
		// TODO: In this case (if padding is -1), the border pixels of each glyph are black, but still computed. For floating-point output, this may play a role.
		if (fixedScale)
			atlasPacker.setScale(config.emSize);
		else
			atlasPacker.setMinimumScale(minEmSize);
		atlasPacker.setPixelRange(pxRange);
		atlasPacker.setMiterLimit(config.miterLimit);
		if (int remaining = atlasPacker.pack(m_MSDFData->Glyphs.data(), (int)m_MSDFData->Glyphs.size()))
		{
			if (remaining < 0)
			{
				LUX_CORE_ASSERT(false);
			}
			else
			{
				LUX_CORE_ERROR_TAG("Renderer", "Error: Could not fit {0} out of {1} glyphs into the atlas.", remaining, (int)m_MSDFData->Glyphs.size());
				LUX_CORE_ASSERT(false);
			}
		}
		atlasPacker.getDimensions(config.width, config.height);
		LUX_CORE_ASSERT(config.width > 0 && config.height > 0);
		config.emSize = atlasPacker.getScale();
		config.pxRange = atlasPacker.getPixelRange();
		if (!fixedScale)
			LUX_CORE_TRACE_TAG("Renderer", "Glyph size: {0} pixels/EM", config.emSize);
		if (!fixedDimensions)
			LUX_CORE_TRACE_TAG("Renderer", "Atlas dimensions: {0} x {1}", config.width, config.height);


		// Edge coloring
		if (config.imageType == ImageType::MSDF || config.imageType == ImageType::MTSDF)
		{
			if (config.expensiveColoring)
			{
				Workload([&glyphs = m_MSDFData->Glyphs, &config](int i, int threadNo) -> bool
					{
						unsigned long long glyphSeed = (LCG_MULTIPLIER * (config.coloringSeed ^ i) + LCG_INCREMENT) * !!config.coloringSeed;
						glyphs[i].edgeColoring(config.edgeColoring, config.angleThreshold, glyphSeed);
						return true;
					}, (int)m_MSDFData->Glyphs.size()).finish(THREADS);
			}
			else
			{
				unsigned long long glyphSeed = config.coloringSeed;
				for (GlyphGeometry& glyph : m_MSDFData->Glyphs)
				{
					glyphSeed *= LCG_MULTIPLIER;
					glyph.edgeColoring(config.edgeColoring, config.angleThreshold, glyphSeed);
				}
			}
		}

		// Check cache here
		const uint64_t cacheKey = GetFontAtlasCacheKey(buffer, config);
		const std::filesystem::path cachePath = GetFontAtlasCachePath(m_Name, config.emSize, cacheKey);
		Buffer storageBuffer;
		if (TryReadFontAtlasFromCache(cachePath, cacheKey, config, storageBuffer))
		{
			const AtlasHeader& header = *storageBuffer.As<AtlasHeader>();
			m_TextureAtlas = CreateCachedAtlas(header, static_cast<uint8_t*>(storageBuffer.Data) + sizeof(AtlasHeader));
			storageBuffer.Release();
		}
		else
		{
			bool floatingPointFormat = true;
			Ref<Texture2D> texture;
			switch (config.imageType)
			{
			case ImageType::MSDF:
				if (floatingPointFormat)
					texture = CreateAndCacheAtlas<float, float, 3, msdfGenerator>(cachePath, cacheKey, m_MSDFData->Glyphs, m_MSDFData->FontGeometry, config);
				else
					texture = CreateAndCacheAtlas<byte, float, 3, msdfGenerator>(cachePath, cacheKey, m_MSDFData->Glyphs, m_MSDFData->FontGeometry, config);
				break;
			case ImageType::MTSDF:
				if (floatingPointFormat)
					texture = CreateAndCacheAtlas<float, float, 4, mtsdfGenerator>(cachePath, cacheKey, m_MSDFData->Glyphs, m_MSDFData->FontGeometry, config);
				else
					texture = CreateAndCacheAtlas<byte, float, 4, mtsdfGenerator>(cachePath, cacheKey, m_MSDFData->Glyphs, m_MSDFData->FontGeometry, config);
				break;
			}

			m_TextureAtlas = texture;
			RemoveSupersededFontAtlases(m_Name, config.emSize, cachePath);
		}
	}

	void Font::Init()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_DefaultFont = Ref<Font>::Create("Resources/Fonts/opensans/OpenSans-Regular.ttf");
		s_DefaultMonoSpacedFont = Ref<Font>::Create("Resources/Fonts/SourceCodePro/static/SourceCodePro-Medium.ttf");
	}

	void Font::Shutdown()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_DefaultMonoSpacedFont.Reset();
		s_DefaultFont.Reset();
	}

	Ref<Font> Font::GetDefaultFont()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_DefaultFont;
	}

	Ref<Font> Font::GetDefaultMonoSpacedFont()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_DefaultMonoSpacedFont;
	}

	Ref<Font> Font::GetFontAssetForTextComponent(const TextComponent& textComponent)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_DefaultFont)
			return nullptr;

		if (!textComponent.FontHandle || textComponent.FontHandle == s_DefaultFont->Handle)
			return s_DefaultFont;

		Ref<AssetManagerBase> assetManager = Project::GetAssetManager();
		if (!assetManager || !assetManager->IsAssetHandleValid(textComponent.FontHandle))
			return s_DefaultFont;

		Ref<Font> font = assetManager->GetAsset(textComponent.FontHandle).As<Font>();
		return font ? font : s_DefaultFont;
	}

}
