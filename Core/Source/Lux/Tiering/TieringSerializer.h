// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
