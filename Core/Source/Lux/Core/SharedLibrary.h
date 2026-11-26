// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>

namespace Lux {

	// Loads a shared library at runtime, for optional libraries the engine runs without (the
	// Nsight Aftermath library). Implemented per platform in Core/Platform/<OS>/<OS>SharedLibrary.cpp.
	class SharedLibrary
	{
	public:
		// `fileName` is the platform file name ("foo.dll", "libfoo.so"), found with the platform's
		// search order (next to the executable on Windows; RUNPATH ($ORIGIN/lib) and
		// LD_LIBRARY_PATH on Linux). Null when the library cannot be loaded.
		static void* Load(const char* fileName);
		// Loads only the supplied file. Use an absolute path for packaged optional SDKs.
		static void* LoadFile(const std::filesystem::path& path);
		// Null when the library does not export `name`.
		static void* GetSymbol(void* library, const char* name);
		static void Unload(void* library);
	};

}
