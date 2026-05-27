#pragma once

#include <string_view>

namespace Lux
{
	namespace Tiering
	{
		namespace Renderer
		{
			enum class ShadowQualitySetting
			{
				None = 0, Low = 1, High = 2
			};

			enum class ShadowResolutionSetting
			{
				None = 0, Low = 1, Medium = 2, High = 3
			};

			enum class AmbientOcclusionTypeSetting
			{
				None = 0, GTAO = 1
			};

			enum class AmbientOcclusionQualitySetting
			{
				None = 0, High = 1, Ultra = 2
			};

			enum class SSRQualitySetting
			{
				Off = 0, Medium = 1, High = 2
			};

			struct RendererTieringSettings
			{
				float RendererScale = 1.0f;
				bool Windowed = false;
				bool VSync = true;

				bool EnableShadows = true;
				ShadowQualitySetting ShadowQuality = ShadowQualitySetting::High;
				ShadowResolutionSetting ShadowResolution = ShadowResolutionSetting::High;

				bool EnableAO = true;
				AmbientOcclusionTypeSetting AOType = AmbientOcclusionTypeSetting::GTAO;
				AmbientOcclusionQualitySetting AOQuality = AmbientOcclusionQualitySetting::Ultra;
				SSRQualitySetting SSRQuality = SSRQualitySetting::Medium;

				bool EnableBloom = true;
			};

			namespace Utils
			{
				const char* ShadowQualitySettingToString(ShadowQualitySetting setting);
				ShadowQualitySetting ShadowQualitySettingFromString(std::string_view setting);

				const char* ShadowResolutionSettingToString(ShadowResolutionSetting setting);
				ShadowResolutionSetting ShadowResolutionSettingFromString(std::string_view setting);

				const char* AmbientOcclusionQualitySettingToString(AmbientOcclusionQualitySetting setting);
				AmbientOcclusionQualitySetting AmbientOcclusionQualitySettingFromString(std::string_view setting);

				const char* SSRQualitySettingToString(SSRQualitySetting setting);
				SSRQualitySetting SSRQualitySettingFromString(std::string_view setting);
			}
		}

		struct TieringSettings
		{
			Renderer::RendererTieringSettings RendererTS;
		};
	}
}
