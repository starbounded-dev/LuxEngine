// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Lux {

	class Scene;

	// Starter content for new projects. Kept in Core so tests and the editor build the same scene.
	namespace SceneTemplates
	{
		// The 2D template's Main scene: an orthographic camera with a solid background, a static ground
		// and a dynamic box above it. Uses no assets.
		void Populate2DStarter(Scene& scene);
	}

}
