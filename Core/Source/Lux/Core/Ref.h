// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Memory.h"

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <typeinfo>

namespace Lux {

	class RefCounted;

	// The weak half of a RefCounted object, created when its first WeakRef is. It outlives the object
	// while WeakRefs remain, so a WeakRef tells a destroyed object from a new one that reuses the
	// address, and WeakRef::Lock can take a strong reference without racing the final release.
	struct WeakControlBlock
	{
		std::atomic<uint32_t> WeakCount = 0;       // one per WeakRef, plus one held by the live object
		std::atomic<RefCounted*> Instance = nullptr; // cleared under Guard when the object is destroyed
		std::atomic_flag Guard;
	};

	namespace RefUtils {
		WeakControlBlock* AcquireWeakControl(const RefCounted& object);
		void ReleaseWeakControl(WeakControlBlock* block);
		void DetachWeakControl(const RefCounted& object);
		// Adds a strong reference to the block's object and returns true, or false when it is
		// destroyed or being destroyed (its count already reached zero).
		bool TryLockWeakControl(WeakControlBlock* block);
		// The same test without taking a reference: true while the object has strong references.
		bool IsWeakControlAlive(WeakControlBlock* block);
	}

	class RefCounted
	{
	public:
		virtual ~RefCounted()
		{
			if (m_WeakControl.load(std::memory_order_acquire))
				RefUtils::DetachWeakControl(*this);
		}

		RefCounted() = default;
		RefCounted(const RefCounted&) {}
		RefCounted& operator=(const RefCounted&) { return *this; }
		RefCounted(RefCounted&&) noexcept {}
		RefCounted& operator=(RefCounted&&) noexcept { return *this; }

		// Returns the new count: 1 means this reference brought the object to life.
		uint32_t IncRefCount() const
		{
			return ++m_RefCount;
		}
		// Returns the new (post-decrement) count so callers can decide deletion from the atomic
		// operation itself. Decrementing and then separately loading the count is a double-free
		// race: two threads can both observe 0 and both delete.
		uint32_t DecRefCount() const
		{
			return --m_RefCount;
		}

		// Increments only a count that is not zero, so a weak lock never revives an object whose last
		// strong reference is already gone.
		bool TryIncRefCount() const
		{
			uint32_t count = m_RefCount.load();
			while (count != 0)
			{
				if (m_RefCount.compare_exchange_weak(count, count + 1))
					return true;
			}
			return false;
		}

		uint32_t GetRefCount() const { return m_RefCount.load(); }
	private:
		mutable std::atomic<uint32_t> m_RefCount = 0;
		// Each object has its own; copying an object never shares its weak references.
		mutable std::atomic<WeakControlBlock*> m_WeakControl = nullptr;

		friend WeakControlBlock* RefUtils::AcquireWeakControl(const RefCounted& object);
		friend void RefUtils::DetachWeakControl(const RefCounted& object);
	};

	template<typename T>
	class Ref
	{
	public:
		Ref()
			: m_Instance(nullptr)
		{
		}
		
		Ref(std::nullptr_t n)
			: m_Instance(nullptr)
		{
		}

		Ref(T* instance)
			: m_Instance(instance)
		{
			static_assert(std::is_base_of<RefCounted, T>::value, "Class is not RefCounted!");

			IncRef();
		}

		template<typename T2>
		requires(std::is_base_of_v<T2, T> || std::is_base_of_v<T, T2>)
		Ref(const Ref<T2>& other)
		{
			m_Instance = (T*)other.m_Instance;
			IncRef();
		}

		template<typename T2>
		requires(std::is_base_of_v<T2, T> || std::is_base_of_v<T, T2>)
		Ref(Ref<T2>&& other)
		{
			m_Instance = (T*)other.m_Instance;
			other.m_Instance = nullptr;
		}

		static Ref<T> CopyWithoutIncrement(const Ref<T>& other)
		{
			Ref<T> result = nullptr;
			result->m_Instance = other.m_Instance;
			return result;
		}

		~Ref()
		{
			DecRef();
		}

		Ref(const Ref<T>& other)
			: m_Instance(other.m_Instance)
		{
			IncRef();
		}

		Ref& operator=(std::nullptr_t)
		{
			DecRef();
			m_Instance = nullptr;
			return *this;
		}

		Ref& operator=(const Ref<T>& other)
		{
			if (this == &other)
				return *this;

			other.IncRef();
			DecRef();

			m_Instance = other.m_Instance;
			return *this;
		}

		template<typename T2>
		Ref& operator=(const Ref<T2>& other)
		{
			other.IncRef();
			DecRef();

			m_Instance = other.m_Instance;
			return *this;
		}

		template<typename T2>
		Ref& operator=(Ref<T2>&& other)
		{
			DecRef();

			m_Instance = other.m_Instance;
			other.m_Instance = nullptr;
			return *this;
		}

		operator bool() { return m_Instance != nullptr; }
		operator bool() const { return m_Instance != nullptr; }

		T* operator->() { return m_Instance; }
		const T* operator->() const { return m_Instance; }

		T& operator*() { return *m_Instance; }
		const T& operator*() const { return *m_Instance; }

		T* Raw() { return  m_Instance; }
		const T* Raw() const { return  m_Instance; }

	void Reset(T* instance = nullptr)
		{
			DecRef();
			m_Instance = instance;
			if (m_Instance)
				IncRef();
		}

		// Alias for compatibility
		void reset(T* instance = nullptr) { Reset(instance); }

		// get() for std::shared_ptr compatibility
		T* get() { return m_Instance; }
		const T* get() const { return m_Instance; }

		template<typename T2>
		requires(std::is_base_of_v<T2, T> || std::is_base_of_v<T, T2>)
		Ref<T2> As() const
		{
			return Ref<T2>(*this);
		}

		template<typename... Args>
		static Ref<T> Create(Args&&... args)
		{
#if LUX_TRACK_MEMORY
			return Ref<T>(new(typeid(T).name()) T(std::forward<Args>(args)...));
#else
			return Ref<T>(new T(std::forward<Args>(args)...));
#endif
		}

		bool operator==(const Ref<T>& other) const
		{
			return m_Instance == other.m_Instance;
		}

		bool operator!=(const Ref<T>& other) const
		{
			return !(*this == other);
		}

		bool EqualsObject(const Ref<T>& other)
		{
			if (!m_Instance || !other.m_Instance)
				return false;
			
			return *m_Instance == *other.m_Instance;
		}
	private:
		void IncRef() const
		{
			if (m_Instance)
				m_Instance->IncRefCount();
		}

		void DecRef() const
		{
			if (m_Instance)
			{
				// Delete only if THIS decrement produced 0. Using the atomic decrement's own result
				// (instead of a separate GetRefCount()==0 load) is what makes cross-thread releases
				// safe: two threads can't both see 0 and double-delete.
				if (m_Instance->DecRefCount() == 0)
				{
					delete m_Instance;
					m_Instance = nullptr;
				}
			}
		}

		template<class T2>
		friend class Ref;
		mutable T* m_Instance;
	};
	
	template<typename T>
	class WeakRef
	{
	public:
		WeakRef() = default;

		WeakRef(const Ref<T>& ref)
			: WeakRef(const_cast<T*>(ref.Raw()))
		{
		}

		// Also valid for an object not yet owned by a Ref (its constructor registering `this`): it
		// reads as dead until a Ref owns it.
		WeakRef(T* instance)
			: m_Instance(instance), m_Control(instance ? RefUtils::AcquireWeakControl(*instance) : nullptr)
		{
		}

		WeakRef(const WeakRef& other)
			: m_Instance(other.m_Instance), m_Control(other.m_Control)
		{
			if (m_Control)
				m_Control->WeakCount.fetch_add(1, std::memory_order_relaxed);
		}

		WeakRef(WeakRef&& other) noexcept
			: m_Instance(other.m_Instance), m_Control(other.m_Control)
		{
			other.m_Instance = nullptr;
			other.m_Control = nullptr;
		}

		WeakRef& operator=(WeakRef other) noexcept
		{
			std::swap(m_Instance, other.m_Instance);
			std::swap(m_Control, other.m_Control);
			return *this;
		}

		~WeakRef()
		{
			RefUtils::ReleaseWeakControl(m_Control);
		}

		// A strong reference that keeps the object alive while it is used, or null once it is
		// destroyed or being destroyed. The only way to reach the object: a validity check followed
		// by a raw access can lose the object to another thread in between.
		Ref<T> Lock() const
		{
			if (!m_Control || !RefUtils::TryLockWeakControl(m_Control))
				return nullptr;

			// TryLockWeakControl's reference keeps the object alive until `strong` holds its own.
			Ref<T> strong(m_Instance);
			m_Instance->DecRefCount();
			return strong;
		}

		// True while the object has strong references. Advisory across threads: use Lock to act on it.
		bool IsValid() const { return m_Control && RefUtils::IsWeakControlAlive(m_Control); }
		operator bool() const { return IsValid(); }
	private:
		T* m_Instance = nullptr;
		WeakControlBlock* m_Control = nullptr;
	};

	// Helper function to create Ref objects (similar to std::make_shared)
	template<typename T, typename... Args>
	Ref<T> CreateRef(Args&&... args)
	{
		return Ref<T>::Create(std::forward<Args>(args)...);
	}

}
