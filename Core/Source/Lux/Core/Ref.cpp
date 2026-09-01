#include "lpch.h"

#include <mutex>
#include <unordered_set>

namespace Lux {

	namespace RefUtils {

		// The live-reference registry is intentionally leaked (heap-allocated, never freed).
		//
		// Static Refs — notably Project::s_AssetManager (inline static Ref<AssetManagerBase>) — are
		// destroyed at program exit, in an order that is undefined across translation units. A plain
		// `static std::unordered_set` here (or even a non-leaked function-local one) is first *used*
		// inside main(), so it is destroyed BEFORE those static Refs; their destructors then call
		// RemoveFromLiveReferences() and erase() into a freed hash table, crashing at shutdown.
		// Leaking the registry guarantees it outlives every Ref, and constructing it on first use also
		// avoids the static-initialization-order fiasco in the other direction.
		//
		// The lock is load-bearing on reads too: an unsynchronized find() racing a concurrent
		// insert()/erase() (which can rehash and reallocate the bucket array) reads a half-freed table.
		// WeakRef::IsValid() calls IsLive() from any thread.
		namespace
		{
			struct LiveReferenceRegistry
			{
				std::unordered_set<void*> References;
				std::mutex Mutex;
			};

			LiveReferenceRegistry& Registry()
			{
				static LiveReferenceRegistry* s_Registry = new LiveReferenceRegistry();
				return *s_Registry;
			}
		}

		void AddToLiveReferences(void* instance)
		{
			LiveReferenceRegistry& registry = Registry();
			std::scoped_lock<std::mutex> lock(registry.Mutex);
			LUX_CORE_ASSERT(instance);
			registry.References.insert(instance);
		}

		void RemoveFromLiveReferences(void* instance)
		{
			LiveReferenceRegistry& registry = Registry();
			std::scoped_lock<std::mutex> lock(registry.Mutex);
			LUX_CORE_ASSERT(instance);
			LUX_CORE_ASSERT(registry.References.find(instance) != registry.References.end());
			registry.References.erase(instance);
		}

		bool IsLive(void* instance)
		{
			LiveReferenceRegistry& registry = Registry();
			std::scoped_lock<std::mutex> lock(registry.Mutex);
			LUX_CORE_ASSERT(instance);
			return registry.References.find(instance) != registry.References.end();
		}
	}

}
