// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Lux::RuntimeExport {

	// Stamps an exported game's executable with its own name, version and icon, so the file shows up as the
	// game in Explorer, the task bar and Task Manager instead of as "Lux-Runtime". Windows edits the
	// executable's resources; elsewhere an executable has no such metadata, so this does nothing and succeeds.
	// iconSource may be empty (the default engine icon stays); a source that cannot be read leaves the icon
	// alone and is reported in outWarning. False only when the executable could not be updated.
	bool BrandExecutable(const std::filesystem::path& executable, const std::filesystem::path& iconSource,
		const std::string& gameName, const std::string& version, std::string& outWarning);

	// The pieces BrandExecutable assembles, exposed so a headless check can verify the bytes without
	// touching an executable. A resource is the raw payload of one RT_ICON / RT_VERSION entry.
	struct IconImage
	{
		uint32_t Size = 0;                // width == height
		std::vector<uint8_t> Resource;    // BITMAPINFOHEADER + 32-bit BGRA pixels + AND mask
	};
	// RGBA8 pixels (row 0 first) to square icon images at the standard sizes, never upscaling past the
	// source. A non-square image is centred on a transparent square.
	std::vector<IconImage> BuildIconImages(const uint8_t* rgba, uint32_t width, uint32_t height);
	// A VS_VERSIONINFO resource with the StringFileInfo entries Explorer shows.
	std::vector<uint8_t> BuildVersionResource(const std::string& gameName, const std::string& version, const std::string& originalFilename);

}
