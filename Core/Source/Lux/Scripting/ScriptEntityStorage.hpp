#pragma once

#include "Lux/Core/UUID.h"
#include "Lux/Core/Buffer.h"

#include <Coral/ManagedObject.hpp>
#include <Coral/Type.hpp>
#include <Coral/Array.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

// Ported from Hazel's ScriptEntityStorage. Per-entity script field VALUES live here (owned by
// the Scene, serialized with it); reflection METADATA lives in ScriptEngine. FieldStorage is
// dual-mode: a serializable byte buffer when idle, a live managed-field proxy when playing.
namespace Lux {

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

	inline const char* DataTypeToString(DataType type)
	{
		switch (type)
		{
			case DataType::SByte: return "SByte";
			case DataType::Byte: return "Byte";
			case DataType::Short: return "Short";
			case DataType::UShort: return "UShort";
			case DataType::Int: return "Int";
			case DataType::UInt: return "UInt";
			case DataType::Long: return "Long";
			case DataType::ULong: return "ULong";
			case DataType::Float: return "Float";
			case DataType::Double: return "Double";
			case DataType::Vector2: return "Vector2";
			case DataType::Vector3: return "Vector3";
			case DataType::Vector4: return "Vector4";
			case DataType::Bool: return "Bool";
			case DataType::Entity: return "Entity";
			case DataType::Prefab: return "Prefab";
			case DataType::Mesh: return "Mesh";
			case DataType::StaticMesh: return "StaticMesh";
			case DataType::Material: return "Material";
			case DataType::Texture2D: return "Texture2D";
			case DataType::Scene: return "Scene";
		}
		return "Float";
	}

	inline DataType DataTypeFromString(std::string_view str)
	{
		if (str == "SByte") return DataType::SByte;
		if (str == "Byte") return DataType::Byte;
		if (str == "Short") return DataType::Short;
		if (str == "UShort") return DataType::UShort;
		if (str == "Int") return DataType::Int;
		if (str == "UInt") return DataType::UInt;
		if (str == "Long") return DataType::Long;
		if (str == "ULong") return DataType::ULong;
		if (str == "Float") return DataType::Float;
		if (str == "Double") return DataType::Double;
		if (str == "Vector2") return DataType::Vector2;
		if (str == "Vector3") return DataType::Vector3;
		if (str == "Vector4") return DataType::Vector4;
		if (str == "Bool") return DataType::Bool;
		if (str == "Entity") return DataType::Entity;
		if (str == "Prefab") return DataType::Prefab;
		if (str == "Mesh") return DataType::Mesh;
		if (str == "StaticMesh") return DataType::StaticMesh;
		if (str == "Material") return DataType::Material;
		if (str == "Texture2D") return DataType::Texture2D;
		if (str == "Scene") return DataType::Scene;
		return DataType::Float;
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
			LUX_CORE_VERIFY(m_Type->IsSZArray());
			return m_ValueBuffer.Size / DataTypeSize(m_DataType);
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
				m_Instance->SetFieldValue(m_Name, value);
			else
				m_ValueBuffer.Write(&value, sizeof(T));
		}

		template<typename T>
		void SetValue(const T& value, uint64_t index)
		{
			LUX_CORE_VERIFY(m_Type->IsSZArray());

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
				return;

			uint64_t copySize = std::min<uint64_t>(size, m_ValueBuffer.Size);
			auto oldBuffer = Buffer::Copy(m_ValueBuffer.Data, copySize);
			m_ValueBuffer.Allocate(size);
			m_ValueBuffer.ZeroInitialize();
			memcpy(m_ValueBuffer.Data, oldBuffer.Data, copySize);
			oldBuffer.Release();
		}

		// Raw buffer access for serialization (buffer holds the serializable field value).
		Buffer& ValueBuffer() { return m_ValueBuffer; }
		const Buffer& ValueBuffer() const { return m_ValueBuffer; }

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

	private:
		std::string m_Name;
		Coral::Type* m_Type = nullptr;
		DataType m_DataType = DataType::Float;
		Buffer m_ValueBuffer;

		Coral::ManagedObject* m_Instance = nullptr;

		friend struct ScriptStorage;
		friend class ScriptEngine;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
	};

	namespace FieldUtils {
		inline bool IsAssetType(DataType type)
		{
			switch (type)
			{
				case DataType::Prefab:     return true;
				case DataType::Mesh:       return true;
				case DataType::StaticMesh: return true;
				case DataType::Material:   return true;
				case DataType::Texture2D:  return true;
				case DataType::Scene:      return true;
				default:                   return false;
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
