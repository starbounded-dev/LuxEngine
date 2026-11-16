// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace Lux::ProjectTemplate
{
	inline constexpr std::array<const char*, 14> DefaultMeshFiles = {
		"Capsule.gltf", "Capsule.bin", "Cone.gltf", "Cone.bin", "Cube.gltf", "Cube.bin",
		"Cylinder.gltf", "Cylinder.bin", "Plane.gltf", "Plane.bin", "Sphere.gltf", "Sphere.bin",
		"Torus.gltf", "Torus.bin"
	};

	// Only source geometry belongs in a new project: imported materials, registries and cached
	// handles belong to the project that created them. Keep an existing project's authored files.
	inline bool CopyDefaultMeshes(const std::filesystem::path& sourceDirectory,
		const std::filesystem::path& destinationDirectory, std::string& error)
	{
		error.clear();
		std::error_code ec;
		for (const char* filename : DefaultMeshFiles)
		{
			const auto source = sourceDirectory / filename;
			if (!std::filesystem::is_regular_file(source, ec))
			{
				error = "Missing default mesh file '" + source.string() + "'";
				if (ec)
					error += ": " + ec.message();
				return false;
			}
		}

		std::filesystem::create_directories(destinationDirectory, ec);
		if (ec)
		{
			error = "Could not create '" + destinationDirectory.string() + "': " + ec.message();
			return false;
		}

		for (const char* filename : DefaultMeshFiles)
		{
			// FileSystem::Copy has no skip-existing mode; copy_file also reports errors without
			// throwing through the editor's project-creation callback.
			const auto destination = destinationDirectory / filename;
			std::filesystem::copy_file(sourceDirectory / filename, destination,
				std::filesystem::copy_options::skip_existing, ec);
			if (ec)
			{
				error = "Could not copy default mesh to '" + destination.string() + "': " + ec.message();
				return false;
			}
		}
		return true;
	}
}
