// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Coral { class ManagedAssembly; }

namespace Lux
{
	// The C# SkeletonComponent: playback control, graph parameters, bone reads and bone attachments.
	class AnimationScriptBindings
	{
	public:
		static void Register(Coral::ManagedAssembly& assembly);
	};
}
