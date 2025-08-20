#pragma once

#include "StarEngine//Core/Assert.h"

#include <array>

namespace StarEngine {

	struct Buffer
	{
		void* Data = nullptr;
		uint64_t Size = 0;

		Buffer() = default;

		Buffer(const void* data, uint64_t size)
			: Data((void*)data), Size(size) {
		}

		template<typename T, size_t S>
		Buffer(const std::array<T, S>& array)
			: Data(array.data()), Size(array.size() * sizeof(T)) {
		}

		template<typename T>
		Buffer(const std::vector<T>& vector)
			: Data(vector.data()), Size(vector.size() * sizeof(T))
		{
		}

		static Buffer Copy(const Buffer& other)
		{
			Buffer buffer;
			buffer.Allocate(other.Size);
			memcpy(buffer.Data, other.Data, other.Size);
			return buffer;
		}

		static Buffer Copy(const void* data, uint64_t size)
		{
			Buffer buffer;
			buffer.Allocate(size);
			if (size) memcpy(buffer.Data, data, size);
			return buffer;
		}

		void Allocate(uint64_t size)
		{
			delete[](byte*)Data;
			Data = nullptr;
			Size = size;

			if (size == 0)
				return;

			Data = snew byte[size];
		}

		void Reallocate(uint64_t size)
		{
			Release();
			Allocate(size);
		}

		void Release()
		{
			delete[](byte*)Data;
			Data = nullptr;
			Size = 0;
		}

		void ZeroInitialize()
		{
			if (Data)
				memset(Data, 0, Size);
		}

		template<typename T>
		T& Read(uint64_t offset = 0)
		{
			return *(T*)((byte*)Data + offset);
		}

		template<typename T>
		const T& Read(uint64_t offset = 0) const
		{
			return *(T*)((byte*)Data + offset);
		}

		byte* ReadBytes(uint64_t size, uint64_t offset) const
		{
			SE_CORE_ASSERT(offset + size <= Size, "Buffer overflow!");
			byte* buffer = snew byte[size];
			memcpy(buffer, (byte*)Data + offset, size);
			return buffer;
		}

		void Write(Buffer buffer, uint64_t offset = 0)
		{
			SE_CORE_ASSERT(offset + buffer.Size <= Size, "Buffer overflow!");
			memcpy((byte*)Data + offset, buffer.Data, buffer.Size);
		}

		void Write(const void* data, uint64_t size, uint64_t offset = 0)
		{
			Write(Buffer(data, size), offset);
		}

		operator bool() const
		{
			return (bool)Data;
		}

		byte& operator[](int index)
		{
			return ((byte*)Data)[index];
		}

		byte operator[](int index) const
		{
			return ((byte*)Data)[index];
		}

		template<typename T>
		T* As() const
		{
			return (T*)Data;
		}

		inline uint64_t GetSize() const { return Size; }
	};

	struct BufferSafe : public Buffer
	{
		~BufferSafe()
		{
			Release();
		}

		static BufferSafe Copy(const void* data, uint64_t size)
		{
			BufferSafe buffer;
			buffer.Allocate(size);
			memcpy(buffer.Data, data, size);
			return buffer;
		}
	};
}
