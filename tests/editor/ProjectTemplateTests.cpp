// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "ProjectTemplate.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	std::string Read(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		Check(stream.is_open(), "Could not read fixture");
		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	}

	void Write(const std::filesystem::path& path, const char* text)
	{
		std::ofstream stream(path, std::ios::binary);
		stream << text;
		stream.close();
		Check(static_cast<bool>(stream), "Could not write fixture");
	}
}

int main(int argc, char** argv)
{
	try
	{
		Check(argc == 3, "Expected resource source and private test output directories");
		const std::filesystem::path bundled = argv[1];
		const std::filesystem::path output = argv[2];
		std::filesystem::create_directories(output);
		std::string error = "previous failure";
		const auto destination = output / "New game with spaces" / "Assets" / "Meshes" / "Source" / "Default";
		Check(Lux::ProjectTemplate::CopyDefaultMeshes(bundled, destination, error), "Fresh initialization failed");
		Check(error.empty(), "Success kept a stale error");
		for (const char* filename : Lux::ProjectTemplate::DefaultMeshFiles)
			Check(Read(bundled / filename) == Read(destination / filename), "Geometry bytes changed during copy");

		Write(destination / "Cube.gltf", "authored geometry");
		Check(Lux::ProjectTemplate::CopyDefaultMeshes(bundled, destination, error), "Repeated initialization failed");
		Check(Read(destination / "Cube.gltf") == "authored geometry", "Authored geometry was overwritten");

		const auto source = output / "Source fixture";
		std::filesystem::copy(bundled, source, std::filesystem::copy_options::recursive);
		Write(source / "AssetRegistry.lzr", "must not travel to another project");
		Write(source / "Material.lmat", "project-local handles");
		const auto filtered = output / "Filtered output";
		Check(Lux::ProjectTemplate::CopyDefaultMeshes(source, filtered, error), "Filtered initialization failed");
		Check(!std::filesystem::exists(filtered / "AssetRegistry.lzr"), "Copied a foreign asset registry");
		Check(!std::filesystem::exists(filtered / "Material.lmat"), "Copied foreign material handles");

		std::filesystem::remove(source / "Torus.bin");
		const auto missing = output / "Missing input output";
		Check(!Lux::ProjectTemplate::CopyDefaultMeshes(source, missing, error), "Missing buffer was accepted");
		Check(error.find("Torus.bin") != std::string::npos, "Missing-file error did not identify the input");
		Check(!std::filesystem::exists(missing), "Missing inputs left a partially seeded destination");

		const auto absent = output / "Absent source output";
		Check(!Lux::ProjectTemplate::CopyDefaultMeshes(output / "No source", absent, error), "Absent resource bundle was accepted");
		Check(!std::filesystem::exists(absent), "Absent source created an output directory");

		const auto blocked = output / "Blocked output";
		Write(blocked, "not a directory");
		Check(!Lux::ProjectTemplate::CopyDefaultMeshes(bundled, blocked, error), "Blocked destination was accepted");
		Check(error.find(blocked.string()) != std::string::npos, "Blocked-output error omitted the path");

		const auto conflict = output / "Directory conflict";
		std::filesystem::create_directories(conflict / "Cube.gltf");
		Check(!Lux::ProjectTemplate::CopyDefaultMeshes(bundled, conflict, error), "A directory was accepted as a mesh file");
		Check(error.find("Cube.gltf") != std::string::npos, "Copy error omitted the conflicting path");
		std::cout << "PASS: fresh/repeated copies, authored files, filtered metadata, missing inputs and copy failures\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL: " << exception.what() << '\n';
		return 1;
	}
}
