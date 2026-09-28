// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Base.h"

#include "Log.h"
#include "Memory.h"
#include "Version.h"

namespace Lux {
	 
	void InitializeCore(const std::filesystem::path& logDirectory)
	{
		Allocator::Init();
		Log::Init(logDirectory);

		LUX_CORE_TRACE_TAG("Core", "Lux Engine {}", LUX_VERSION);
		LUX_CORE_TRACE_TAG("Core", "Initializing...");
	}

	void ShutdownCore()
	{
		LUX_CORE_TRACE_TAG("Core", "Shutting down...");
		
		Log::Shutdown();
	}

}
