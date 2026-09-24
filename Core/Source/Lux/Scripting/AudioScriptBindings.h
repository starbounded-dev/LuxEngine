// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Coral { class ManagedAssembly; }

namespace Lux {
	class AudioScriptBindings
	{
	public:
		static void Register(Coral::ManagedAssembly& assembly);
		static void Update(bool paused);
		static void Reset();
		static void Shutdown();
	};
}
