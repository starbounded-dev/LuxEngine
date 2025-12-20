#include "sepch.h"
#include "Base.h"

#include "Log.h"
#include "Memory.h"
#include "Version.h"

namespace StarEngine {

	void InitializeCore()
	{
		Allocator::Init();
		Log::Init();

		SE_CORE_TRACE_TAG("Core", "StarEngine Engine {}", SE_VERSION);
		SE_CORE_TRACE_TAG("Core", "Initializing...");
	}

	void ShutdownCore()
	{
		SE_CORE_TRACE_TAG("Core", "Shutting down...");

		Log::Shutdown();
	}

}
