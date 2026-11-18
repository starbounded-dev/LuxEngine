// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>

namespace Lux::RuntimeSplash
{
	// The optional image an exported game shows while it starts (Project Settings > Runtime Export > Splash).
	// On Windows it is a borderless window of its own, on its own thread so it stays alive through the long,
	// blocking load; Hide closes it. Elsewhere both do nothing. Main thread only; Hide is safe to repeat.
	void Show(const std::filesystem::path& image);
	void Hide();
	// The window's class name, so a test can look for it.
	inline constexpr const wchar_t* WindowClass = L"LuxGameSplash";
}
