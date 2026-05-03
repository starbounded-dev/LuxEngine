#pragma once

#include "Lux/Serialization/StreamWriter.h"
#include "Lux/Serialization/StreamReader.h"

#include "Lux/Core/Assert.h"
#include "Lux/Core/Base.h"
#include "Lux/Core/UUID.h"
#include "Lux/Reflection/TypeDescriptor.h"

#include <algorithm>
#include <array>
#include <string>
#include <type_traits>
#include <vector>

// Non-intrusive binary / memory serialization interface.
// Add SERIALIZABLE(Class, &Class::MemberA, &Class::MemberB) in a header for
// member-by-member serialization.
#define SERIALIZABLE(Class, ...) template<>\
struct Lux::Type::Description<Class> : Lux::Type::MemberList<__VA_ARGS__> {};

namespace Lux::Serialization
{
	template<typename... Ts>
	static bool Serialize(StreamWriter* writer, const Ts&... vs);

	namespace Impl
	{
		template<typename T>
		static bool SerializeImpl(StreamWriter* writer, const T& v)
		{
			if constexpr (std::is_trivial_v<T>)
				writer->WriteRaw(v);
			else
				T::Serialize(writer, v);

			return true;
		}

		template<>
		LUX_EXPLICIT_STATIC inline bool SerializeImpl(StreamWriter* writer, const UUID& v)
		{
			writer->WriteRaw((uint64_t)v);
			return true;
		}

		template<>
		LUX_EXPLICIT_STATIC inline bool SerializeImpl(StreamWriter* writer, const std::string& v)
		{
			writer->WriteString(v);
			return true;
		}

		template<typename T>
		LUX_EXPLICIT_STATIC bool SerializeVec(StreamWriter* writer, const std::vector<T>& vec)
		{
			writer->WriteRaw<uint32_t>((uint32_t)vec.size());

			for (const auto& element : vec)
				Serialize(writer, element);

			return true;
		}

		template<typename T, size_t Size>
		LUX_EXPLICIT_STATIC bool SerializeVec(StreamWriter* writer, const std::array<T, Size>& array)
		{
			writer->WriteRaw<uint32_t>((uint32_t)array.size());

			for (const auto& element : array)
				Serialize(writer, element);

			return true;
		}

		template<typename T>
		LUX_EXPLICIT_STATIC bool SerializeByType(StreamWriter* writer, const T& v)
		{
			if constexpr (Type::Described<T>::value)
			{
				return Type::Description<T>::Apply(
					[&writer](const auto&... members)
					{
						return Serialize(writer, members...);
					}, v);
			}
			else if constexpr (Type::is_array_v<T>)
			{
				return SerializeVec(writer, v);
			}
			else
			{
				return SerializeImpl(writer, v);
			}
		}
	} // namespace Impl

	template<typename... Ts>
	static bool Serialize(StreamWriter* writer, const Ts&... vs)
	{
		return (Impl::SerializeByType(writer, vs) && ...);
	}

	template<typename... Ts>
	static bool Deserialize(StreamReader* reader, Ts&... vs);

	namespace Impl
	{
		template<typename T>
		static bool DeserializeImpl(StreamReader* reader, T& v)
		{
			if constexpr (std::is_trivial_v<T>)
				reader->ReadRaw(v);
			else
				T::Deserialize(reader, v);

			return true;
		}

		template<>
		LUX_EXPLICIT_STATIC inline bool DeserializeImpl(StreamReader* reader, UUID& v)
		{
			uint64_t data = 0;
			reader->ReadRaw(data);
			v = UUID(data);
			return true;
		}

		template<>
		LUX_EXPLICIT_STATIC inline bool DeserializeImpl(StreamReader* reader, std::string& v)
		{
			reader->ReadString(v);
			return true;
		}

		template<typename T>
		LUX_EXPLICIT_STATIC bool DeserializeVec(StreamReader* reader, std::vector<T>& vec)
		{
			uint32_t size = 0;
			reader->ReadRaw<uint32_t>(size);

			vec.resize(size);

			for (uint32_t i = 0; i < size; i++)
				Deserialize(reader, vec[i]);

			return true;
		}

		template<typename T, size_t Size>
		LUX_EXPLICIT_STATIC bool DeserializeVec(StreamReader* reader, std::array<T, Size>& array)
		{
			uint32_t size = 0;
			reader->ReadRaw<uint32_t>(size);
			LUX_CORE_ASSERT(size == Size);

			for (uint32_t i = 0; i < std::min<uint32_t>(size, (uint32_t)Size); i++)
				Deserialize(reader, array[i]);

			return size == Size;
		}

		template<typename T>
		LUX_EXPLICIT_STATIC bool DeserializeByType(StreamReader* reader, T& v)
		{
			if constexpr (Type::Described<T>::value)
			{
				return Type::Description<T>::Apply(
					[&reader](auto&... members)
					{
						return Deserialize(reader, members...);
					}, v);
			}
			else if constexpr (Type::is_array_v<T>)
			{
				return DeserializeVec(reader, v);
			}
			else
			{
				return DeserializeImpl(reader, v);
			}
		}
	} // namespace Impl

	template<typename... Ts>
	static bool Deserialize(StreamReader* reader, Ts&... vs)
	{
		return (Impl::DeserializeByType(reader, vs) && ...);
	}
} // namespace Lux::Serialization
