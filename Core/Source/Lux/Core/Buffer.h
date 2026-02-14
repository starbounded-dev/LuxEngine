#pragma once

#include "Lux/Core/Assert.h"

#include <array>

namespace Lux {

	struct Buffer
	{
		void* Data = nullptr;
		uint64_t Size = 0;

		Buffer() = default;

		// Constructor that allocates a buffer of the specified size
		explicit Buffer(uint64_t size)
		{
			Allocate(size);
		}

		Buffer(const void* data, uint64_t size)
			: Data((void*)data), Size(size) { }

		template<typename T, size_t S>
		Buffer(const std::array<T, S>& array)
			: Data(array.data()), Size(array.size() * sizeof(T)) { }

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
			if (size)
				memcpy(buffer.Data, data, size);
			return buffer;
		}

		void Allocate(uint64_t size)
		{
			delete[] (byte*)Data;
			Data = nullptr;
			Size = size;

			if (size == 0)
				return;

			Data = lnew byte[size];
		}

		void Reallocate(uint64_t size)
		{
			Release();
			Allocate(size);
		}

		void Release()
		{
			delete[] (byte*)Data;
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
			LUX_CORE_VERIFY(offset + size <= Size, "Buffer overflow!");
			byte* buffer = lnew byte[size];
			memcpy(buffer, (byte*)Data + offset, size);
			return buffer;
		}
				
		void Write(Buffer buffer, uint64_t offset = 0)
		{
			LUX_CORE_VERIFY(offset + buffer.Size <= Size, "Buffer overflow!");
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

	struct ScopedBuffer
	{
		ScopedBuffer(Buffer buffer)
			: m_Buffer(buffer)
		{
		}

		ScopedBuffer(uint64_t size)
			: m_Buffer(size)
		{
		}

		~ScopedBuffer()
		{
			m_Buffer.Release();
		}

		uint8_t* Data() { return (uint8_t*)m_Buffer.Data; }
		uint64_t Size() { return m_Buffer.Size; }

		template<typename T>
		T* As()
		{
			return m_Buffer.As<T>();
		}

		operator bool() const { return (bool)m_Buffer; }
	private:
		Buffer m_Buffer;
	};

	template<uint64_t MaxSize>
	struct StaticBuffer
	{
		uint8_t Data[MaxSize];
		uint64_t Size = 0;

		template<typename T>
		void Write(const T& data, uint64_t offset = 0)
		{
			constexpr size_t dataSize = sizeof(T);
			LUX_CORE_VERIFY(offset + dataSize <= MaxSize, "Buffer overflow!");
			memcpy((byte*)Data + offset, &data, dataSize);

			if (Size < offset + dataSize)
				Size = offset + dataSize;
		}

		void Write(Buffer buffer, uint64_t offset = 0)
		{
			LUX_CORE_VERIFY(offset + buffer.Size <= MaxSize, "Buffer overflow!");
			memcpy((byte*)Data + offset, buffer.Data, buffer.Size);

			if (Size < offset + buffer.Size)
				Size = offset + buffer.Size;
		}
	};
}
