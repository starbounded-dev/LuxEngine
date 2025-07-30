#pragma once

#include "StarEngine/Core/UUID.h"
#include "StarEngine/Core/Buffer.h"

#include <Coral/ManagedObject.hpp>
#include <Coral/Type.hpp>
#include <Coral/Array.hpp>

namespace StarEngine {

	enum class DataType
	{
		SByte,
		Byte,
		Short,
		UShort,
		Int,
		UInt,
		Long,
		ULong,

		Float,
		Double,

		Vector2,
		Vector3,
		Vector4,

		Bool,

		String,

		Entity,
		Prefab,
		Mesh,
		StaticMesh,
		Material,
		Texture2D,
		Scene
	};

	inline uint64_t DataTypeSize(DataType type)
	{
		switch (type)
		{
			case DataType::SByte: return sizeof(int8_t);
			case DataType::Byte: return sizeof(uint8_t);
			case DataType::Short: return sizeof(int16_t);
			case DataType::UShort: return sizeof(uint16_t);
			case DataType::Int: return sizeof(int32_t);
			case DataType::UInt: return sizeof(uint32_t);
			case DataType::Long: return sizeof(int64_t);
			case DataType::ULong: return sizeof(uint64_t);
			case DataType::Float: return sizeof(float);
			case DataType::Double: return sizeof(double);
			case DataType::Vector2: return sizeof(float) * 2;
			case DataType::Vector3: return sizeof(float) * 3;
			case DataType::Vector4: return sizeof(float) * 4;
			case DataType::Bool: return sizeof(Coral::Bool32);
			case DataType::String: return sizeof(Coral::ScopedString);
			case DataType::Entity: return sizeof(UUID);
			case DataType::Prefab: return sizeof(UUID);
			case DataType::Mesh: return sizeof(UUID);
			case DataType::StaticMesh: return sizeof(UUID);
			case DataType::Material: return sizeof(UUID);
			case DataType::Texture2D: return sizeof(UUID);
			case DataType::Scene: return sizeof(UUID);
		}

		return 0;
	}

	struct FieldMetadata;

	class FieldStorage
	{
	public:
		std::string_view GetName() const { return m_Name; }
		DataType GetType() const { return m_DataType; }
		bool IsArray() const { return m_Type->IsSZArray(); }

		uint64_t GetLength() const
		{
			if (m_Type->IsSZArray())
			{
				return m_ValueBuffer.Size / DataTypeSize(m_DataType);
			}
			else if (m_DataType == DataType::String) {
				return m_ValueBuffer.Size;
			}
			else {
				SE_CORE_VERIFY(false, "Invalid field type for `GetLength()`");
				return 0;
			}
			
			if (m_Instance)
			{
				auto arr = m_Instance->GetFieldValue<Coral::Array<int32_t>>(m_Name);
				int32_t length = arr.Length();
				Coral::Array<int32_t>::Free(arr);
				return length;
			}

		}

		template<typename T>
		T GetValue() const
		{
			return m_Instance ? m_Instance->GetFieldValue<T>(m_Name) : m_ValueBuffer.Read<T>();
		}

		template<typename T>
		T GetValue(uint32_t index) const
		{
			if (m_Instance)
			{
				auto arr = m_Instance->GetFieldValue<Coral::Array<T>>(m_Name);
				T value = arr[index];
				Coral::Array<T>::Free(arr);
				return value;
			}

			return m_ValueBuffer.Read<T>(index * sizeof(T));
		}

		template<typename T>
		void SetValue(const T& value)
		{
			if (m_Instance)
			{
				m_Instance->SetFieldValue(m_Name, value);
			}
			else
			{
				m_ValueBuffer.Write(&value, sizeof(T));
			}
		}

		template<typename T>
		void SetValue(const T& value, uint64_t index)
		{
			SE_CORE_VERIFY(m_Type->IsSZArray());

			if (m_Instance)
			{
				auto arr = m_Instance->GetFieldValue<Coral::Array<T>>(m_Name);
				arr[index] = value;
				m_Instance->SetFieldValue(m_Name, arr);
				Coral::Array<T>::Free(arr);
			}
			else
			{
				uint64_t offset = index * sizeof(T);
				m_ValueBuffer.Write(&value, sizeof(T), offset);
			}
		}

		void Resize(uint64_t newLength)
		{
			uint64_t size = newLength * DataTypeSize(m_DataType);

			if (m_Instance)
			{
				/*auto arr = m_Instance->GetFieldValue<Coral::Array<int32_t>>(m_Name);
				auto newArr = Coral::Array<int32_t>::New(arr.Length() + 1);
				newArr.Assign(arr);
				Coral::Array<int32_t>::Free(arr);
				m_Instance->SetFieldValue(m_Name, newArr);
				Coral::Array<int32_t>::Free(newArr);*/
			}
			else
			{
				uint64_t copySize = std::min<uint64_t>(size, m_ValueBuffer.Size);
				auto oldBuffer = Buffer::Copy(m_ValueBuffer.Data, copySize);
				m_ValueBuffer.Allocate(size);
				m_ValueBuffer.ZeroInitialize();
				memcpy(m_ValueBuffer.Data, oldBuffer.Data, copySize);
				oldBuffer.Release();
			}
		}

		void RemoveAt(uint64_t index)
		{
			uint64_t newLength = GetLength() - 1;

			auto oldBuffer = Buffer::Copy(m_ValueBuffer);
			m_ValueBuffer.Release();
			m_ValueBuffer.Allocate(newLength * DataTypeSize(m_DataType));

			if (index != 0)
			{
				uint64_t indexOffset = index * DataTypeSize(m_DataType);
				memcpy(m_ValueBuffer.Data, oldBuffer.Data, indexOffset);
				memcpy(
					reinterpret_cast<std::byte*>(m_ValueBuffer.Data) + indexOffset,
					reinterpret_cast<std::byte*>(oldBuffer.Data) + indexOffset + DataTypeSize(m_DataType),
					(newLength - index) * DataTypeSize(m_DataType)
				);
			}
			else
			{
				memcpy(m_ValueBuffer.Data, reinterpret_cast<std::byte*>(oldBuffer.Data) + DataTypeSize(m_DataType), newLength * DataTypeSize(m_DataType));
			}

			oldBuffer.Release();
		}

		FieldStorage Copy()
		{
			FieldStorage copy;

			copy.m_Name = m_Name;
			copy.m_Type = m_Type;
			copy.m_DataType = m_DataType;
			copy.m_ValueBuffer = Buffer::Copy(m_ValueBuffer);
			copy.m_Instance = m_Instance;

			return copy;
		}

	private:
		std::string m_Name;
		Coral::Type* m_Type;
		DataType m_DataType;
		Buffer m_ValueBuffer;

		Coral::ManagedObject* m_Instance;

		friend struct ScriptStorage;
		friend class ScriptEngine;
	};

	template<>
	inline std::string FieldStorage::GetValue() const
	{
		if (m_Instance)
		{
			return m_Instance->GetFieldValue<std::string>(m_Name);
		}
		else
		{
			std::string retval;
			retval.resize(m_ValueBuffer.Size - 1);
			memcpy(retval.data(), m_ValueBuffer.Data, m_ValueBuffer.Size - 1);
			return retval;
		}
	}

	template<>
	inline std::string FieldStorage::GetValue(uint32_t index) const
	{
		/*if (m_Instance)
		{
			auto arr = m_Instance->GetFieldValue<Coral::Array<T>>(m_Name);
			T value = arr[index];
			Coral::Array<T>::Free(arr);
			return value;
		}

		return m_ValueBuffer.Read<T>(index * sizeof(T));*/

		SE_CORE_VERIFY(false, "String array fields not currently supported");
		return {};
	}

	template<>
	inline void FieldStorage::SetValue(const std::string& value)
	{
		if (m_Instance)
		{
			m_Instance->SetFieldValue(m_Name, Coral::String::New(value));
		}
		else
		{
			m_ValueBuffer.Allocate(value.size() + 1);
			m_ValueBuffer.Write(value.data(), value.size() + 1);
		}
	}

	template<>
	inline void FieldStorage::SetValue(const std::string& value, uint64_t index)
	{
		SE_CORE_VERIFY(m_Type->IsSZArray());

		/*if (m_Instance)
		{
			auto arr = m_Instance->GetFieldValue<Coral::Array<T>>(m_Name);
			arr[index] = value;
			m_Instance->SetFieldValue(m_Name, arr);
			Coral::Array<T>::Free(arr);
		}
		else
		{
			uint64_t offset = index * sizeof(T);
			m_ValueBuffer.Write(&value, sizeof(T), offset);
		}*/

		SE_CORE_VERIFY(false, "String array fields not currently supported");
	}

	namespace FieldUtils {
		inline bool IsAssetType(DataType type)
		{
			switch (type)
			{
				case DataType::Prefab:          return true;
				case DataType::Mesh:            return true;
				case DataType::StaticMesh:      return true;
				case DataType::Material:        return true;
				case DataType::Texture2D:       return true;
				case DataType::Scene:			return true;
				default:
					return false;
			}
		}
	}

	struct EntityScriptStorage
	{
		UUID ScriptID;
		std::unordered_map<uint32_t, FieldStorage> Fields;
		Coral::ManagedObject* Instance = nullptr;
	};

	struct ScriptStorage
	{
		std::unordered_map<UUID, EntityScriptStorage> EntityStorage;

		void InitializeEntityStorage(UUID scriptID, UUID entityID);
		void ShutdownEntityStorage(UUID scriptID, UUID entityID);

		void SynchronizeStorage();

		void CopyTo(ScriptStorage& other) const;
		void CopyEntityStorage(UUID entityID, UUID targetEntityID, ScriptStorage& targetStorage) const;
		void Clear();

	private:
		void InitializeFieldStorage(EntityScriptStorage& storage, uint32_t fieldID, const FieldMetadata& fieldMetadata);
	};

}
