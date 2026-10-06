// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Core/SharedLibrary.h"

#include <Windows.h>

namespace Lux {

	void* SharedLibrary::Load(const char* fileName)
	{
		return static_cast<void*>(LoadLibraryA(fileName));
	}

	void* SharedLibrary::GetSymbol(void* library, const char* name)
	{
		return library ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name)) : nullptr;
	}

	void SharedLibrary::Unload(void* library)
	{
		if (library)
			FreeLibrary(static_cast<HMODULE>(library));
	}

}
