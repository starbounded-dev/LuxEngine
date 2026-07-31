#include "lpch.h"

#include <unordered_set>

namespace Lux {

	static std::unordered_set<void*> s_LiveReferences;
	static std::mutex s_LiveReferenceMutex;

	namespace RefUtils {

		void AddToLiveReferences(void* instance)
		{
			std::scoped_lock<std::mutex> lock(s_LiveReferenceMutex);
			LUX_CORE_ASSERT(instance);
			s_LiveReferences.insert(instance);
		}

		void RemoveFromLiveReferences(void* instance)
		{
			std::scoped_lock<std::mutex> lock(s_LiveReferenceMutex);
			LUX_CORE_ASSERT(instance);
			LUX_CORE_ASSERT(s_LiveReferences.find(instance) != s_LiveReferences.end());
			s_LiveReferences.erase(instance);
		}

		bool IsLive(void* instance)
		{
			// MUST hold the same lock as Add/Remove: an unsynchronized find() racing with a
			// concurrent insert()/erase() (which can rehash and reallocate the bucket array) reads a
			// half-freed hash table, corrupting s_LiveReferences. That corruption surfaced later as a
			// crash in erase() during shutdown. WeakRef::IsValid() calls this from any thread.
			std::scoped_lock<std::mutex> lock(s_LiveReferenceMutex);
			LUX_CORE_ASSERT(instance);
			return s_LiveReferences.find(instance) != s_LiveReferences.end();
		}
	}


}
