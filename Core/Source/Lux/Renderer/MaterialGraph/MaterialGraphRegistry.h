// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "MaterialGraph.h"

#include <filesystem>
#include <vector>

namespace Lux {

	// Turns the open project's material graphs into the shader include the G-buffer and transparent
	// passes call (Include/GLSL/MaterialGraph.glslh): one function per graph, picked by the material
	// row's graph index. Every graph lives in those two shaders, so graph materials keep the
	// GPU-driven single-pipeline path; the cost is that a graph edit recompiles both shaders.
	//
	// Editor only: at run time the shaders come from the exported shader pack and each material's
	// graph index from the asset pack. Main thread, except Register / Unregister (any thread).
	class MaterialGraphRegistry
	{
	public:
		// Resources/Cache/Shader/Generated/MaterialGraphs.glslh, an include search path.
		static std::filesystem::path GetGeneratedIncludePath();
		// Writes the no-graph include when there is none, so the shaders compile. Before any shader loads.
		static void EnsureGeneratedInclude();

		// A material that owns a graph was loaded or created (or no longer has one). Applied by Update.
		static void Register(AssetHandle material);
		static void Unregister(AssetHandle material);
		// A registered graph changed: Update regenerates.
		static void MarkDirty();

		// Applies pending changes: rescans after a project switch, regenerates the include, and when it
		// changed reloads the surface shaders and rebuilds the graph materials' rows. Cheap when nothing
		// changed; the editor calls it every frame.
		static void Update();
		// Loads every material in the project whose file has a graph, then updates. Before an export,
		// so the shader pack has every graph.
		static void ScanProject();

		// What the last update found wrong with the material's graph (empty when it compiled).
		static std::vector<MaterialGraph::Error> GetErrors(AssetHandle material);
	};

}
