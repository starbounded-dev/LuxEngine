// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Coral { class ManagedAssembly; }

namespace Lux
{
	class Scene;

	// The C# Game UI components (UICanvasComponent, UIElementComponent, UIImageComponent, UITextComponent,
	// UIWidgetComponent) and their events. Scalars cross as (field, value) pairs, as the other bindings do.
	class UIScriptBindings
	{
	public:
		static void Register(Coral::ManagedAssembly& assembly);
		// After the internal calls are uploaded: finds the managed event type.
		static void Bind(Coral::ManagedAssembly& assembly);

		// Hands this frame's UI events (clicks, toggles, slider moves, text edits, cancel) to the managed handlers
		// scripts registered. Main thread, right after the scene's UI update.
		static void Dispatch(Scene& scene);

		// Drops every managed handler: a scene ended, and its entities' handlers must not fire in the next one.
		static void Reset();
		// Drops the managed type before an assembly unload.
		static void Shutdown();
	};
}
