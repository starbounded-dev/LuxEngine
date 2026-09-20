// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <map>
#include <vector>

namespace Lux {

	template<typename T>
	class SBuffer
	{
	public:

	private:
		T* m_Buffer = nullptr;
	};

	template<typename T, bool SerializeSize = true>
	class SArray
	{
	public:

	private:
		std::vector<T> m_Array;
	};

	template<typename Key, typename Value, bool SerializeSize = true>
	class SMap
	{
	public:

	private:
		std::map<Key, Value> m_Map;
	};

}
