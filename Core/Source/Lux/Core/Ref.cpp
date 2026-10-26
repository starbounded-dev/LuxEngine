// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"

#include <thread>

namespace Lux {

	namespace RefUtils {

		namespace
		{
			// Guard is held only around reading Instance and touching the object's count, so a spin is
			// cheaper than a mutex here and keeps the block small.
			class ScopedWeakGuard
			{
			public:
				explicit ScopedWeakGuard(WeakControlBlock& block)
					: m_Block(block)
				{
					while (m_Block.Guard.test_and_set(std::memory_order_acquire))
						std::this_thread::yield();
				}

				~ScopedWeakGuard()
				{
					m_Block.Guard.clear(std::memory_order_release);
				}
			private:
				WeakControlBlock& m_Block;
			};
		}

		WeakControlBlock* AcquireWeakControl(const RefCounted& object)
		{
			WeakControlBlock* block = object.m_WeakControl.load(std::memory_order_acquire);
			if (!block)
			{
				// The object keeps one count on its block until it is destroyed (DetachWeakControl).
				WeakControlBlock* created = lnew WeakControlBlock();
				created->WeakCount.store(2, std::memory_order_relaxed);
				created->Instance.store(const_cast<RefCounted*>(&object), std::memory_order_relaxed);
				if (object.m_WeakControl.compare_exchange_strong(block, created, std::memory_order_acq_rel))
					return created;

				// Another thread created the block first; `block` is now theirs.
				ldelete created;
			}

			block->WeakCount.fetch_add(1, std::memory_order_relaxed);
			return block;
		}

		void ReleaseWeakControl(WeakControlBlock* block)
		{
			if (block && block->WeakCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
				ldelete block;
		}

		void DetachWeakControl(const RefCounted& object)
		{
			// Runs in ~RefCounted, after the derived destructors but before the memory is freed. A
			// TryLockWeakControl holding the guard is touching the count, so wait for it; after the
			// guard is released no lock can reach the object.
			WeakControlBlock* block = object.m_WeakControl.load(std::memory_order_acquire);
			{
				ScopedWeakGuard guard(*block);
				block->Instance.store(nullptr, std::memory_order_relaxed);
			}
			ReleaseWeakControl(block);
		}

		bool TryLockWeakControl(WeakControlBlock* block)
		{
			ScopedWeakGuard guard(*block);
			RefCounted* instance = block->Instance.load(std::memory_order_relaxed);
			return instance && instance->TryIncRefCount();
		}

		bool IsWeakControlAlive(WeakControlBlock* block)
		{
			ScopedWeakGuard guard(*block);
			RefCounted* instance = block->Instance.load(std::memory_order_relaxed);
			return instance && instance->GetRefCount() > 0;
		}
	}

}
