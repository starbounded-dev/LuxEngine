#include "lpch.h"
#include "TieringSettings.h"

namespace Lux
{
	namespace Tiering
	{
		namespace Renderer::Utils
		{
			const char* ShadowQualitySettingToString(ShadowQualitySetting setting)
			{
				switch (setting)
				{
					case ShadowQualitySetting::None: return "None";
					case ShadowQualitySetting::Low: return "Low";
					case ShadowQualitySetting::High: return "High";
				}

				return "None";
			}

			ShadowQualitySetting ShadowQualitySettingFromString(std::string_view setting)
			{
				if (setting == "None") return ShadowQualitySetting::None;
				if (setting == "Low") return ShadowQualitySetting::Low;
				if (setting == "High") return ShadowQualitySetting::High;
				return ShadowQualitySetting::None;
			}

			const char* ShadowResolutionSettingToString(ShadowResolutionSetting setting)
			{
				switch (setting)
				{
					case ShadowResolutionSetting::None: return "None";
					case ShadowResolutionSetting::Low: return "Low";
					case ShadowResolutionSetting::Medium: return "Medium";
					case ShadowResolutionSetting::High: return "High";
				}

				return "None";
			}

			ShadowResolutionSetting ShadowResolutionSettingFromString(std::string_view setting)
			{
				if (setting == "None") return ShadowResolutionSetting::None;
				if (setting == "Low") return ShadowResolutionSetting::Low;
				if (setting == "Medium") return ShadowResolutionSetting::Medium;
				if (setting == "High") return ShadowResolutionSetting::High;
				return ShadowResolutionSetting::None;
			}

			const char* AmbientOcclusionQualitySettingToString(AmbientOcclusionQualitySetting setting)
			{
				switch (setting)
				{
					case AmbientOcclusionQualitySetting::None: return "None";
					case AmbientOcclusionQualitySetting::High: return "High";
					case AmbientOcclusionQualitySetting::Ultra: return "Ultra";
				}

				return "None";
			}

			AmbientOcclusionQualitySetting AmbientOcclusionQualitySettingFromString(std::string_view setting)
			{
				if (setting == "None") return AmbientOcclusionQualitySetting::None;
				if (setting == "Low") return AmbientOcclusionQualitySetting::High;
				if (setting == "High") return AmbientOcclusionQualitySetting::High;
				if (setting == "Ultra") return AmbientOcclusionQualitySetting::Ultra;
				return AmbientOcclusionQualitySetting::None;
			}

			const char* SSRQualitySettingToString(SSRQualitySetting setting)
			{
				switch (setting)
				{
					case SSRQualitySetting::Off: return "Off";
					case SSRQualitySetting::Medium: return "Medium";
					case SSRQualitySetting::High: return "High";
				}

				return "Off";
			}

			SSRQualitySetting SSRQualitySettingFromString(std::string_view setting)
			{
				if (setting == "Off") return SSRQualitySetting::Off;
				if (setting == "Medium") return SSRQualitySetting::Medium;
				if (setting == "High") return SSRQualitySetting::High;
				return SSRQualitySetting::Off;
			}
		}
	}
}
