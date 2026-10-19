// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ScriptEntityStorage.hpp"
#include "Lux/Scene/Scene.h"
#include "ScriptEngine.h"

#include <optional>

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

		// Values kept while the script was unresolved belong to it now; ones kept for a
		// different script are stale (the entity's class changed).
		auto unresolvedIt = UnresolvedStorage.find(entityID);
		if (unresolvedIt != UnresolvedStorage.end())
		{
			if (unresolvedIt->second.ScriptID == scriptID)
			{
				for (const StoredField& field : unresolvedIt->second.Fields)
					ApplyStoredField(entityStorage, field);
			}

			UnresolvedStorage.erase(unresolvedIt);
		}
	}

	void ScriptStorage::ShutdownEntityStorage(UUID scriptID, UUID entityID)
	{
		const auto& scriptEngine = ScriptEngine::GetInstance();

		LUX_CORE_VERIFY(scriptEngine.IsValidScript(scriptID));
		LUX_CORE_VERIFY(EntityStorage.contains(entityID));

		ReleaseEntityStorage(entityID);
	}

	void ScriptStorage::RestoreEntityFields(UUID scriptID, UUID entityID, std::vector<StoredField> fields)
	{
		if (!ScriptEngine::GetInstance().IsValidScript(scriptID))
		{
			ReleaseEntityStorage(entityID);

			if (fields.empty())
				UnresolvedStorage.erase(entityID);
			else
				UnresolvedStorage[entityID] = { scriptID, std::move(fields) };
			return;
		}

		// The file's values replace whatever was kept or initialized for the entity before.
		UnresolvedStorage.erase(entityID);

		auto storageIt = EntityStorage.find(entityID);
		if (storageIt != EntityStorage.end() && storageIt->second.ScriptID != scriptID)
		{
			ReleaseEntityStorage(entityID);
			storageIt = EntityStorage.end();
		}

		if (storageIt == EntityStorage.end())
			InitializeEntityStorage(scriptID, entityID);

		auto& entityStorage = EntityStorage.at(entityID);
		for (const StoredField& field : fields)
			ApplyStoredField(entityStorage, field);
	}

	const UnresolvedScriptStorage* ScriptStorage::FindUnresolvedStorage(UUID scriptID, UUID entityID) const
	{
		auto it = UnresolvedStorage.find(entityID);
		if (it == UnresolvedStorage.end() || it->second.ScriptID != scriptID)
			return nullptr;

		return &it->second;
	}

	void ScriptStorage::SynchronizeStorage()
	{
		const auto& scriptEngine = ScriptEngine::GetInstance();

		for (auto entityIt = EntityStorage.begin(); entityIt != EntityStorage.end();)
		{
			auto& [entityID, entityStorage] = *entityIt;

			// The script left the assembly, or no assembly is loaded: keep the values for a save
			// or a later reload instead of dropping them. A live instance (a reload during Play)
			// keeps its entry: DestroyInstance expects it when the scene stops.
			if (!scriptEngine.IsValidScript(entityStorage.ScriptID))
			{
				if (entityStorage.Instance)
				{
					++entityIt;
					continue;
				}

				UnresolvedScriptStorage& unresolved = UnresolvedStorage[entityID];
				unresolved.ScriptID = entityStorage.ScriptID;
				unresolved.Fields.clear();

				for (auto& [fieldID, fieldStorage] : entityStorage.Fields)
				{
					StoredField& field = unresolved.Fields.emplace_back();
					field.ID = fieldID;
					field.Name = fieldStorage.m_Name;
					field.Type = fieldStorage.m_DataType;
					field.Array = fieldStorage.m_IsArray;

					const auto* bytes = static_cast<const std::byte*>(fieldStorage.m_ValueBuffer.Data);
					field.Value.assign(bytes, bytes + fieldStorage.m_ValueBuffer.Size);
					fieldStorage.m_ValueBuffer.Release();
				}

				// The order a resolved save writes, so a save across the reload changes nothing.
				std::sort(unresolved.Fields.begin(), unresolved.Fields.end(), [](const StoredField& a, const StoredField& b) { return a.ID < b.ID; });

				entityIt = EntityStorage.erase(entityIt);
				continue;
			}

			const auto& scriptMetadata = scriptEngine.GetScriptMetadata(entityStorage.ScriptID);

			// Drop fields the script no longer declares: Instantiate looks each stored field up.
			for (auto fieldIt = entityStorage.Fields.begin(); fieldIt != entityStorage.Fields.end();)
			{
				if (scriptMetadata.Fields.contains(fieldIt->first))
				{
					++fieldIt;
					continue;
				}

				fieldIt->second.m_ValueBuffer.Release();
				fieldIt = entityStorage.Fields.erase(fieldIt);
			}

			for (const auto& [fieldID, fieldMetadata] : scriptMetadata.Fields)
			{
				auto fieldIt = entityStorage.Fields.find(fieldID);
				if (fieldIt != entityStorage.Fields.end())
				{
					FieldStorage& fieldStorage = fieldIt->second;
					if (fieldStorage.m_DataType == fieldMetadata.Type && fieldStorage.m_IsArray == fieldMetadata.ManagedType->IsSZArray())
					{
						fieldStorage.m_Type = fieldMetadata.ManagedType;
						continue;
					}

					// The field changed type; its old value no longer fits.
					fieldStorage.m_ValueBuffer.Release();
				}

				InitializeFieldStorage(entityStorage, fieldID, fieldMetadata);
			}

			++entityIt;
		}

		// Scripts the assembly has now take the values kept for them.
		std::vector<UUID> resolvedEntities;
		for (const auto& [entityID, unresolved] : UnresolvedStorage)
		{
			if (scriptEngine.IsValidScript(unresolved.ScriptID) && !EntityStorage.contains(entityID))
				resolvedEntities.push_back(entityID);
		}

		for (UUID entityID : resolvedEntities)
			InitializeEntityStorage(UnresolvedStorage.at(entityID).ScriptID, entityID);
	}

	void ScriptStorage::CopyTo(ScriptStorage& other) const
	{
		other.Clear();

		// Copied verbatim; SynchronizeStorage below settles anything the loaded assembly changed.
		for (const auto& [entityID, entityStorage] : EntityStorage)
		{
			auto& otherStorage = other.EntityStorage[entityID];
			otherStorage.ScriptID = entityStorage.ScriptID;
			otherStorage.Instance = nullptr;

			for (const auto& [fieldID, fieldStorage] : entityStorage.Fields)
			{
				CopyFieldStorage(fieldStorage, otherStorage.Fields[fieldID]);
			}
		}

		other.UnresolvedStorage = UnresolvedStorage;
		other.SynchronizeStorage();
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

			CopyFieldStorage(fieldStorage, dstStorage.Fields[fieldID]);
		}
	}

	void ScriptStorage::CopyEntityFields(UUID entityID, ScriptStorage& target, UUID targetEntityID) const
	{
		if (&target == this && entityID == targetEntityID)
			return;

		if (auto targetIt = target.EntityStorage.find(targetEntityID); targetIt != target.EntityStorage.end() && targetIt->second.Instance)
			return;

		// Copy before touching the target: it may be this storage, and inserting may rehash it.
		std::optional<UnresolvedScriptStorage> unresolvedCopy;
		if (auto unresolvedIt = UnresolvedStorage.find(entityID); unresolvedIt != UnresolvedStorage.end())
			unresolvedCopy = unresolvedIt->second;

		target.ReleaseEntityStorage(targetEntityID);
		target.UnresolvedStorage.erase(targetEntityID);

		if (unresolvedCopy)
		{
			target.UnresolvedStorage[targetEntityID] = std::move(*unresolvedCopy);
			return;
		}

		auto sourceIt = EntityStorage.find(entityID);
		if (sourceIt == EntityStorage.end())
			return;

		// Element references survive a rehash of the same map, iterators do not.
		const EntityScriptStorage& source = sourceIt->second;
		EntityScriptStorage& destination = target.EntityStorage[targetEntityID];
		destination.ScriptID = source.ScriptID;
		destination.Instance = nullptr;

		for (const auto& [fieldID, fieldStorage] : source.Fields)
			CopyFieldStorage(fieldStorage, destination.Fields[fieldID]);
	}

	void ScriptStorage::Clear()
	{
		for (auto& [entityID, entityStorage] : EntityStorage)
		{
			for (auto& [fieldID, fieldStorage] : entityStorage.Fields)
				fieldStorage.m_ValueBuffer.Release();
		}

		EntityStorage.clear();
		UnresolvedStorage.clear();
	}

	void ScriptStorage::InitializeFieldStorage(EntityScriptStorage& storage, uint32_t fieldID, const FieldMetadata& fieldMetadata)
	{
		auto& fieldStorage = storage.Fields[fieldID];
		fieldStorage.m_Name = fieldMetadata.Name;
		fieldStorage.m_Type = fieldMetadata.ManagedType;
		fieldStorage.m_DataType = fieldMetadata.Type;
		fieldStorage.m_IsArray = fieldMetadata.ManagedType->IsSZArray();

		if (fieldMetadata.DefaultValue.Data == nullptr || fieldMetadata.DefaultValue.Size == 0)
		{
			// No captured default: a zero value (null reference), or an empty array. Allocate does
			// not clear, and heap garbage here reached C# and scene files as real values.
			fieldStorage.m_ValueBuffer.Allocate(fieldStorage.m_IsArray ? 0 : DataTypeSize(fieldMetadata.Type));
			fieldStorage.m_ValueBuffer.ZeroInitialize();
		}
		else
		{
			fieldStorage.m_ValueBuffer = Buffer::Copy(fieldMetadata.DefaultValue);
		}

		fieldStorage.m_Instance = nullptr;
	}

	void ScriptStorage::ReleaseEntityStorage(UUID entityID)
	{
		auto it = EntityStorage.find(entityID);
		if (it == EntityStorage.end())
			return;

		for (auto& [fieldID, fieldStorage] : it->second.Fields)
			fieldStorage.m_ValueBuffer.Release();

		EntityStorage.erase(it);
	}

	void ScriptStorage::CopyFieldStorage(const FieldStorage& source, FieldStorage& destination)
	{
		destination.m_ValueBuffer.Release();
		destination.m_Name = source.m_Name;
		destination.m_Type = source.m_Type;
		destination.m_DataType = source.m_DataType;
		destination.m_IsArray = source.m_IsArray;
		destination.m_ValueBuffer = Buffer::Copy(source.m_ValueBuffer);
		destination.m_Instance = nullptr;
	}

	void ScriptStorage::ApplyStoredField(EntityScriptStorage& storage, const StoredField& field)
	{
		auto it = storage.Fields.find(field.ID);
		if (it == storage.Fields.end())
			return;   // the script no longer declares the field

		FieldStorage& fieldStorage = it->second;
		if (fieldStorage.m_DataType != field.Type || fieldStorage.m_IsArray != field.Array)
		{
			LUX_CORE_WARN_TAG("Scripting", "Field '{}' changed type since it was saved ({} -> {}); it keeps the script default.",
				field.Name, DataTypeToString(field.Type), DataTypeToString(fieldStorage.m_DataType));
			return;
		}

		// Scene files restore idle storage; Instantiate hands the values to the managed object.
		Buffer& buffer = fieldStorage.m_ValueBuffer;
		if (field.Array)
		{
			buffer.Allocate(field.Value.size());
			if (!field.Value.empty())
				std::memcpy(buffer.Data, field.Value.data(), field.Value.size());
		}
		else if (field.Value.size() == buffer.Size)
		{
			std::memcpy(buffer.Data, field.Value.data(), field.Value.size());
		}
	}

}
