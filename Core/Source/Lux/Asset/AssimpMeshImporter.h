#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/Mesh.h"

#include <filesystem>

namespace Lux
{
	// Imports a mesh file from disk using the Assimp library and populates
	// a MeshSource asset with vertices, indices, submeshes and basic material handles.
	class AssimpMeshImporter
	{
	public:
		explicit AssimpMeshImporter(const std::filesystem::path& path);

		// Load the mesh source.  Returns nullptr on failure.
		Ref<MeshSource> ImportToMeshSource();

	private:
		std::filesystem::path m_Path;
	};
}
