#pragma once

#include "Lux/Project/TieringSettings.h"

#include <filesystem>

namespace Lux
{
	class TieringSerializer
	{
	public:
		static bool Serialize(const Tiering::TieringSettings& tieringSettings, const std::filesystem::path& filepath);
		static bool Deserialize(Tiering::TieringSettings& outTieringSettings, const std::filesystem::path& filepath);
	};
}
