#include "lpch.h"
#include "ScriptEntityStorage.hpp"
#include "Lux/Scene/Scene.h"
#include "ScriptEngine.h"

namespace Lux {

	void ScriptStorage::InitializeEntityStorage(UUID scriptID, UUID entityID)
	{
		const auto& scriptEngine = ScriptEngine::GetInstance();

		LUX_CORE_VERIFY(scriptEngine.IsValidScript(scriptID));
		LUX_CORE_VERIFY(!EntityStorage.contains(entityID));

		const auto& scriptMetadata = scriptEngine.GetScriptMetadata(scriptID);

		auto& entityStorage = EntityStorage[entityID];
		entityStorage.ScriptID = scriptID;

		for (const auto& [fieldID, fieldMetadata] : scriptMetadata.Fields)
			InitializeFieldStorage(entityStorage, fieldID, fieldMetadata);
	}

	void ScriptStorage::ShutdownEntityStorage(UUID scriptID, UUID entityID)
	{
		const auto& scriptEngine = ScriptEngine::GetInstance();

		LUX_CORE_VERIFY(scriptEngine.IsValidScript(scriptID));
		LUX_CORE_VERIFY(EntityStorage.contains(entityID));

		for (auto& [fieldID, fieldStorage] : EntityStorage[entityID].Fields)
			fieldStorage.m_ValueBuffer.Release();

		EntityStorage.erase(entityID);
	}

	void ScriptStorage::SynchronizeStorage()
	{
		const auto& scriptEngine = ScriptEngine::GetInstance();

		for (auto& [entityID, entityStorage] : EntityStorage)
		{
			if (!scriptEngine.IsValidScript(entityStorage.ScriptID))
				continue;

			const auto& scriptMetadata = scriptEngine.GetScriptMetadata(entityStorage.ScriptID);

			for (const auto& [fieldID, fieldMetadata] : scriptMetadata.Fields)
			{
				if (entityStorage.Fields.contains(fieldID))
				{
					entityStorage.Fields[fieldID].m_Type = fieldMetadata.ManagedType;
					continue;
				}

				InitializeFieldStorage(entityStorage, fieldID, fieldMetadata);
			}
		}
	}

	void ScriptStorage::CopyTo(ScriptStorage& other) const
	{
		const auto& scriptEngine = ScriptEngine::GetInstance();

		for (const auto& [entityID, entityStorage] : EntityStorage)
		{
			if (!scriptEngine.IsValidScript(entityStorage.ScriptID))
			{
				LUX_CORE_ERROR("[ScriptStorage] Cannot copy script data for script ID {}. The script is no longer valid", entityStorage.ScriptID);
				continue;
			}

			const auto& scriptMetadata = scriptEngine.GetScriptMetadata(entityStorage.ScriptID);

			auto& otherStorage = other.EntityStorage[entityID];
			otherStorage.ScriptID = entityStorage.ScriptID;
			otherStorage.Instance = nullptr;

			for (const auto& [fieldID, fieldStorage] : entityStorage.Fields)
			{
				if (!scriptMetadata.Fields.contains(fieldID))
				{
					LUX_CORE_ERROR("[ScriptStorage] Cannot copy field '{}'. The field is no longer in the script.", fieldStorage.GetName());
					continue;
				}

				auto& otherFieldStorage = otherStorage.Fields[fieldID];
				otherFieldStorage.m_Name = fieldStorage.m_Name;
				otherFieldStorage.m_Type = fieldStorage.m_Type;
				otherFieldStorage.m_DataType = fieldStorage.m_DataType;
				otherFieldStorage.m_ValueBuffer = Buffer::Copy(fieldStorage.m_ValueBuffer);
				otherFieldStorage.m_Instance = nullptr;
			}
		}
	}

	void ScriptStorage::CopyEntityStorage(UUID entityID, UUID targetEntityID, ScriptStorage& targetStorage) const
	{
		if (!targetStorage.EntityStorage.contains(targetEntityID))
		{
			LUX_CORE_ERROR("[ScriptStorage] Cannot copy storage to entity {} - InitializeEntityStorage was not called for it.", targetEntityID);
			return;
		}

		const auto& scriptEngine = ScriptEngine::GetInstance();
		const auto& srcStorage = EntityStorage.at(entityID);

		if (!scriptEngine.IsValidScript(srcStorage.ScriptID))
		{
			LUX_CORE_ERROR("[ScriptStorage] Cannot copy script data for script ID {}. The script is no longer valid", srcStorage.ScriptID);
			return;
		}

		auto& dstStorage = targetStorage.EntityStorage.at(targetEntityID);

		if (dstStorage.ScriptID != srcStorage.ScriptID)
		{
			LUX_CORE_ERROR("[ScriptStorage] Cannot copy storage from {} to {} - different scripts.", entityID, targetEntityID);
			return;
		}

		const auto& scriptMetadata = scriptEngine.GetScriptMetadata(srcStorage.ScriptID);

		dstStorage.Instance = nullptr;

		for (const auto& [fieldID, fieldStorage] : srcStorage.Fields)
		{
			if (!scriptMetadata.Fields.contains(fieldID))
			{
				LUX_CORE_ERROR("[ScriptStorage] Cannot copy field '{}'. The field is no longer in the script.", fieldStorage.GetName());
				continue;
			}

			auto& otherFieldStorage = dstStorage.Fields[fieldID];
			otherFieldStorage.m_Name = fieldStorage.m_Name;
			otherFieldStorage.m_Type = fieldStorage.m_Type;
			otherFieldStorage.m_DataType = fieldStorage.m_DataType;
			otherFieldStorage.m_ValueBuffer = Buffer::Copy(fieldStorage.m_ValueBuffer);
			otherFieldStorage.m_Instance = nullptr;
		}
	}

	void ScriptStorage::Clear()
	{
		for (auto& [entityID, entityStorage] : EntityStorage)
		{
			for (auto& [fieldID, fieldStorage] : entityStorage.Fields)
				fieldStorage.m_ValueBuffer.Release();
		}

		EntityStorage.clear();
	}

	void ScriptStorage::InitializeFieldStorage(EntityScriptStorage& storage, uint32_t fieldID, const FieldMetadata& fieldMetadata)
	{
		auto& fieldStorage = storage.Fields[fieldID];
		fieldStorage.m_Name = fieldMetadata.Name;
		fieldStorage.m_Type = fieldMetadata.ManagedType;
		fieldStorage.m_DataType = fieldMetadata.Type;

		if (fieldMetadata.DefaultValue.Data == nullptr || fieldMetadata.DefaultValue.Size == 0)
			fieldStorage.m_ValueBuffer.Allocate(DataTypeSize(fieldMetadata.Type));
		else
			fieldStorage.m_ValueBuffer = Buffer::Copy(fieldMetadata.DefaultValue);

		fieldStorage.m_Instance = nullptr;
	}

}
