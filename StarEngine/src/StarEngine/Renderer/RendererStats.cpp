#include "sepch.h"
#include "RendererStats.h"

namespace StarEngine {

	namespace RendererUtils {

		static ResourceAllocationCounts s_ResourceAllocationCounts;
		ResourceAllocationCounts& GetResourceAllocationCounts()
		{
			return s_ResourceAllocationCounts;
		}

	}

}
