// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Coral { class ManagedAssembly; }

namespace Lux
{
	// The C# lights, camera, StaticMeshComponent (mesh, material slots, per-entity material values) and
	// MusicDirectorComponent. Scalars cross as (kind, field) pairs, as the audio and animation bindings do.
	class ComponentScriptBindings
	{
	public:
		static void Register(Coral::ManagedAssembly& assembly);
	};
}
