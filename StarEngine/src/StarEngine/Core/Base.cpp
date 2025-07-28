#include "sepch.h"
#include "Base.h"

#include "Log.h"
#include "Memory.h"

namespace StarEngine {

	void InitializeCore()
	{
		Allocator::Init();
		Log::Init();

		SE_CORE_TRACE_TAG("Core", "StarEngine {}", SE_VERSION);
		SE_CORE_TRACE_TAG("Core", "Intializing...");
	}

	void ShutdownCore()
	{
		SE_CORE_TRACE_TAG("Core", "Shutting down...");

		Log::Shutdown();
	}

}
