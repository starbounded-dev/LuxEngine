// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <span>

namespace Lux::ImGuiEx
{
	// IDs belong to the caller and remain stable when actions move into the overflow menu.
	// The controls own no document, selection, callbacks or persistent state.
	struct PanelAction
	{
		int ID;
		const char* Label;
		const char* Icon = nullptr;
		const char* Shortcut = nullptr;
		bool Enabled = true;
		bool Selected = false;
		bool Primary = false;
		bool MenuOnly = false;
	};

	// Main thread, inside a window. Returns an activated action ID, or -1.
	// Primary actions retain space first, then compact to icons before moving into More.
	int PanelActionBar(const char* id, std::span<const PanelAction> actions);
	int PanelEmptyState(const char* id, const char* title, const char* description,
						std::span<const PanelAction> actions = {});
	void PanelMessage(const char* title, const char* description, bool error = false);
} // namespace Lux::ImGuiEx
