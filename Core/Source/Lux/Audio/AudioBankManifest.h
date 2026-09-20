// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Lux
{
	class StreamReader;
	class StreamWriter;

	// Explicitly serialized after ProjectInfo's fixed header in runtime format 17 and later.
	struct AudioBankManifest
	{
		std::filesystem::path Directory;
		std::vector<std::string> Banks;
		bool EnableLiveUpdate = false;

		bool Validate() const;
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};
}
