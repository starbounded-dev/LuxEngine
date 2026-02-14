#include "lpch.h"
#include "Base.h"

#include "Log.h"
#include "Memory.h"
#include "Version.h"

namespace Lux {
	 
	void InitializeCore()
	{
		Allocator::Init();
		Log::Init();

		LUX_CORE_TRACE_TAG("Core", "Lux Engine {}", LUX_VERSION);
		LUX_CORE_TRACE_TAG("Core", "Initializing...");
	}

	void ShutdownCore()
	{
		LUX_CORE_TRACE_TAG("Core", "Shutting down...");
		
		Log::Shutdown();
	}

}
