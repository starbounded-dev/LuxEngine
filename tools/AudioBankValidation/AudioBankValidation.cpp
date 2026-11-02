// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "Lux/Audio/AudioValidation.h"
#include "Lux/Core/Log.h"

#include <exception>
#include <filesystem>
#include <iostream>

// EntryPoint normally supplies this Core lifetime flag; this console host creates no Application.
bool g_ApplicationRunning = false;

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: Lux-AudioBankValidation <bank-directory> <report-directory>\n";
		return 2;
	}
	try
	{
		Lux::Log::Init(std::filesystem::path(argv[2]));
		Lux::Log::EnabledTags()["Audio"] = { true, Lux::Log::Level::Info };
		// Validate the real built bank catalog through the same NOSOUND host used by export.
		// Project-reference validation remains the editor/export's separate responsibility.
		const auto report = Lux::AudioValidation::ValidateBanks(std::filesystem::path(argv[1]), {});
		report.Log();
		const bool passed = !report.Banks.empty() && !report.HasErrors();
		if (report.Banks.empty())
			LUX_CORE_ERROR_TAG("Audio", "Bank validation requires at least one readable built bank: {}", argv[1]);
		Lux::Log::Shutdown();
		return passed ? 0 : 1;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Audio bank validation failed: " << error.what() << '\n';
		return 1;
	}
}
