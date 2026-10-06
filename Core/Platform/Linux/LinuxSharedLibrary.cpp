// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Core/SharedLibrary.h"

#include <dlfcn.h>

namespace Lux {

	void* SharedLibrary::Load(const char* fileName)
	{
		return dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
	}

	void* SharedLibrary::GetSymbol(void* library, const char* name)
	{
		return library ? dlsym(library, name) : nullptr;
	}

	void SharedLibrary::Unload(void* library)
	{
		if (library)
			dlclose(library);
	}

}
