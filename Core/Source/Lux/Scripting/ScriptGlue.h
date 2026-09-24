// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Coral {
	class ManagedAssembly;
}

namespace Lux {

	class ScriptGlue
	{
	public:
		// Registers component add/has/remove maps and all internal calls onto the core
		// assembly, then uploads them. Called once per assembly load/reload.
		static void RegisterGlue(Coral::ManagedAssembly& coreAssembly);

		// Gamepad connect/disconnect events for C# (Lux.Input.GamepadConnected/Disconnected).
		// UpdateInput diffs the connected set once per runtime frame; the first call after a
		// reset only records it. ResetInput also clears the managed handlers (Play start/stop);
		// ShutdownInput drops the managed type before an assembly unload.
		static void UpdateInput();
		static void ResetInput();
		static void ShutdownInput();
	};

}
