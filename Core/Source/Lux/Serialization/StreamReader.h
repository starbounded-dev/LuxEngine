// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Buffer.h"

namespace Lux
{
	class StreamReader
	{
	public:
		virtual ~StreamReader() = default;

		virtual bool IsStreamGood() const = 0;
		virtual uint64_t GetStreamPosition() = 0;
		virtual void SetStreamPosition(uint64_t position) = 0;
		virtual bool ReadData(char* destination, size_t size) = 0;
		virtual uint64_t GetStreamSize() = 0;

		operator bool() const { return IsStreamGood(); }

		// Bytes left to read. Lengths and counts read from the stream are checked against it, so a
		// damaged file fails the read instead of allocating (or looping over) whatever it claims.
		uint64_t GetRemainingBytes()
		{
			const uint64_t position = GetStreamPosition();
			const uint64_t size = GetStreamSize();
			return position < size ? size - position : 0;
		}

		bool ReadBuffer(Buffer& buffer, uint32_t size = 0);
		bool ReadString(std::string& string);

		// False on a short read. No assert: shipped files (asset packs, the runtime project, shader packs)
		// can be damaged, and a Debug player must report that, not stop.
		template<typename T>
		bool ReadRaw(T& type)
		{
			return ReadData((char*)&type, sizeof(T));
		}

		template<typename T>
		void ReadObject(T& obj)
		{
			T::Deserialize(this, obj);
		}

		template<typename Key, typename Value>
		bool ReadMap(std::map<Key, Value>& map, uint32_t size = 0)
		{
			if (size == 0)
				ReadRaw<uint32_t>(size);
			if (!CountFits(size, MinimumSize<Key>() + MinimumSize<Value>()))
				return false;

			for (uint32_t i = 0; i < size; i++)
			{
				Key key;
				if (!ReadElement(key) || !ReadElement(map[key]))
					return false;
			}
			return true;
		}

		template<typename Key, typename Value>
		bool ReadMap(std::unordered_map<Key, Value>& map, uint32_t size = 0)
		{
			if (size == 0)
				ReadRaw<uint32_t>(size);
			if (!CountFits(size, MinimumSize<Key>() + MinimumSize<Value>()))
				return false;

			for (uint32_t i = 0; i < size; i++)
			{
				Key key;
				if (!ReadElement(key) || !ReadElement(map[key]))
					return false;
			}
			return true;
		}

		template<typename Value>
		bool ReadMap(std::unordered_map<std::string, Value>& map, uint32_t size = 0)
		{
			if (size == 0)
				ReadRaw<uint32_t>(size);
			if (!CountFits(size, sizeof(size_t) + MinimumSize<Value>()))
				return false;

			for (uint32_t i = 0; i < size; i++)
			{
				std::string key;
				if (!ReadString(key) || !ReadElement(map[key]))
					return false;
			}
			return true;
		}

		template<typename T>
		bool ReadArray(std::vector<T>& array, uint32_t size = 0)
		{
			if (size == 0)
				ReadRaw<uint32_t>(size);
			if (!CountFits(size, MinimumSize<T>()))
			{
				array.clear();
				return false;
			}

			array.resize(size);

			for (uint32_t i = 0; i < size; i++)
			{
				if (!ReadElement(array[i]))
					return false;
			}
			return true;
		}

	private:
		// The fewest bytes one element can take in the stream (a non-trivial object: at least one).
		template<typename T>
		static constexpr uint64_t MinimumSize() { if constexpr (std::is_trivial<T>()) return sizeof(T); else return 1; }

		bool CountFits(uint64_t count, uint64_t elementSize) { return count <= GetRemainingBytes() / elementSize; }

		// ReadObject reports nothing, so a non-trivial element counts as read.
		template<typename T>
		bool ReadElement(T& element)
		{
			if constexpr (std::is_trivial<T>())
				return ReadRaw<T>(element);
			else
			{
				ReadObject<T>(element);
				return true;
			}
		}
	};

	template<>
	inline bool StreamReader::ReadArray(std::vector<std::string>& array, uint32_t size)
	{
		if (size == 0)
			ReadRaw<uint32_t>(size);
		if (!CountFits(size, sizeof(size_t)))
		{
			array.clear();
			return false;
		}

		array.resize(size);

		for (uint32_t i = 0; i < size; i++)
		{
			if (!ReadString(array[i]))
				return false;
		}
		return true;
	}
} // namespace Lux
