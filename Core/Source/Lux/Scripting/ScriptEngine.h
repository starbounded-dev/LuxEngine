#pragma once

#include "CSharpObject.h"
#include "ScriptEntityStorage.hpp"

#include "Lux/Core/Ref.h"
#include "Lux/Core/Buffer.h"

#include <Coral/Assembly.hpp>
#include <Coral/Type.hpp>
#include <Coral/StableVector.hpp>
#include <Coral/Attribute.hpp>
#include <Coral/Array.hpp>

#include <filesystem>
#include <unordered_set>

namespace Coral {
	class HostInstance;
	class ManagedAssembly;
	class AssemblyLoadContext;
}

namespace Lux {

	class Scene;
	class Project;

	struct AssemblyData
	{
		Coral::ManagedAssembly* Assembly = nullptr;
		std::unordered_map<UUID, Coral::Type*> CachedTypes;
	};

	struct FieldMetadata
	{
		std::string Name;
		DataType Type = DataType::Float;
		Coral::Type* ManagedType = nullptr;

		// Editor hints from C# [Range]/[Header]/[Tooltip], read in BuildAssemblyCache.
		bool HasRange = false;
		float RangeMin = 0.0f;
		float RangeMax = 1.0f;
		std::string Header;
		std::string Tooltip;

		Buffer DefaultValue;

	private:
		template<typename T>
		void SetDefaultValue(Coral::ManagedObject& temp)
		{
			if (ManagedType->IsSZArray())
			{
				auto value = temp.GetFieldValue<Coral::Array<T>>(Name);
				DefaultValue = Buffer::Copy(value.Data(), value.ByteLength());
				Coral::Array<T>::Free(value);
			}
			else
			{
				DefaultValue.Allocate(sizeof(T));
				auto value = temp.GetFieldValue<T>(Name);
				DefaultValue.Write(&value, sizeof(T));
			}
		}

		friend class ScriptEngine;
	};

	struct ScriptMetadata
	{
		std::string FullName;
		std::unordered_map<uint32_t, FieldMetadata> Fields;

		// Names of methods the user's class declares (incl. OnCreate/OnUpdate/OnDestroy). Used to
		// avoid invoking lifecycle hooks the script doesn't define.
		std::unordered_set<std::string> Methods;

		bool HasMethod(const std::string& name) const { return Methods.contains(name); }
	};

	class ScriptEngine
	{
	public:
		// Defined out-of-line so Scene need not be complete in this header.
		Ref<Scene> GetCurrentScene() const;
		void SetCurrentScene(Ref<Scene> scene);

		bool IsValidScript(UUID scriptID) const;

		const ScriptMetadata& GetScriptMetadata(UUID scriptID) const;
		const std::unordered_map<UUID, ScriptMetadata>& GetAllScripts() const { return m_ScriptMetadata; }

		const Coral::Type* GetTypeByName(std::string_view name) const;

		bool IsHostInitialized() const { return m_Host != nullptr; }
		bool IsAppAssemblyLoaded() const { return m_AppAssemblyData != nullptr && m_AppAssemblyData->Assembly != nullptr; }

		Coral::ManagedAssembly& GetCoreAssembly() const { return *m_CoreAssemblyData->Assembly; }

	public:
		static const ScriptEngine& GetInstance();

		// Outcome of the most recent project-assembly load, surfaced by the editor as a reload toast.
		struct ReloadStatus
		{
			bool Success = false;
			std::string Message;
			uint32_t ScriptCount = 0;
		};
		const ReloadStatus& GetLastReloadStatus() const { return m_LastReloadStatus; }

	private:
		void InitializeHost();
		void ShutdownHost();

		void Initialize(Ref<Project> project);
		void Shutdown();

		void LoadProjectAssembly();
		void LoadProjectAssemblyRuntime(Buffer data);

		// Editor hot reload: unload the ALC, reload core + app, rebuild caches, re-register glue.
		void ReloadAppAssembly();

		void BuildAssemblyCache(AssemblyData* assemblyData);

		std::filesystem::path ShadowCopyAssembly(const std::filesystem::path& originalAbsolute);

		template<typename... TArgs>
		CSharpObject Instantiate(UUID entityID, ScriptStorage& storage, TArgs&&... args)
		{
			LUX_CORE_VERIFY(storage.EntityStorage.contains(entityID));

			auto& entityStorage = storage.EntityStorage.at(entityID);

			if (!IsValidScript(entityStorage.ScriptID))
				return {};

			auto* type = m_AppAssemblyData->CachedTypes[entityStorage.ScriptID];
			auto instance = type->CreateInstance(std::forward<TArgs>(args)...);
			auto [index, handle] = m_ManagedObjects.Insert(std::move(instance));

			entityStorage.Instance = &handle;

			auto& editorAssignableAttribType = m_CoreAssemblyData->Assembly->GetLocalType("Lux.EditorAssignableAttribute");

			for (auto& [fieldID, fieldStorage] : entityStorage.Fields)
			{
				const auto& fieldMetadata = m_ScriptMetadata[entityStorage.ScriptID].Fields[fieldID];

				if (editorAssignableAttribType && fieldMetadata.ManagedType->HasAttribute(editorAssignableAttribType))
				{
					Coral::ManagedObject value = fieldMetadata.ManagedType->CreateInstance(fieldStorage.GetValue<uint64_t>());
					handle.SetFieldValue(fieldStorage.GetName(), value);
					value.Destroy();
				}
				else if (fieldMetadata.ManagedType->IsSZArray())
				{
					if (editorAssignableAttribType && fieldMetadata.ManagedType->GetElementType().HasAttribute(editorAssignableAttribType))
					{
						Coral::Array<Coral::ManagedObject> arr = Coral::Array<Coral::ManagedObject>::New((int32_t)fieldStorage.GetLength());

						for (int32_t i = 0; i < (int32_t)fieldStorage.GetLength(); i++)
							arr[i] = fieldMetadata.ManagedType->GetElementType().CreateInstance(fieldStorage.GetValue<uint64_t>(i));

						handle.SetFieldValue(fieldStorage.GetName(), arr);

						for (int32_t i = 0; i < (int32_t)fieldStorage.GetLength(); i++)
							arr[i].Destroy();

						Coral::Array<Coral::ManagedObject>::Free(arr);
					}
					else
					{
						struct ArrayContainer
						{
							void* Data;
							int32_t Length;
						} array;

						array.Data = fieldStorage.m_ValueBuffer.Data;
						array.Length = static_cast<int32_t>(fieldStorage.GetLength());

						handle.SetFieldValueRaw(fieldStorage.GetName(), &array);
					}
				}
				else
				{
					handle.SetFieldValueRaw(fieldStorage.GetName(), fieldStorage.m_ValueBuffer.Data);
				}

				fieldStorage.m_Instance = &handle;
			}

			CSharpObject result;
			result.m_Handle = &handle;
			return result;
		}

		void DestroyInstance(UUID entityID, ScriptStorage& storage)
		{
			LUX_CORE_VERIFY(storage.EntityStorage.contains(entityID));

			auto& entityStorage = storage.EntityStorage.at(entityID);

			if (!IsValidScript(entityStorage.ScriptID) || !entityStorage.Instance)
				return;

			for (auto& [fieldID, fieldStorage] : entityStorage.Fields)
				fieldStorage.m_Instance = nullptr;

			entityStorage.Instance->Destroy();
			entityStorage.Instance = nullptr;
		}

	private:
		static ScriptEngine& GetMutable();

	private:
		ScriptEngine() = default;

		ScriptEngine(const ScriptEngine&) = delete;
		ScriptEngine(ScriptEngine&&) = delete;
		ScriptEngine& operator=(const ScriptEngine&) = delete;
		ScriptEngine& operator=(ScriptEngine&&) = delete;

	private:
		std::unique_ptr<Coral::HostInstance> m_Host;
		std::unique_ptr<Coral::AssemblyLoadContext> m_LoadContext;
		Scope<AssemblyData> m_CoreAssemblyData = nullptr;
		Scope<AssemblyData> m_AppAssemblyData = nullptr;

		std::unordered_map<UUID, ScriptMetadata> m_ScriptMetadata;

		ReloadStatus m_LastReloadStatus;

		Ref<Scene> m_CurrentScene = nullptr;
		Coral::StableVector<Coral::ManagedObject> m_ManagedObjects;

		std::filesystem::path m_ShadowDirRoot;
		uint32_t m_ShadowCounter = 0;

	private:
		friend class Application;
		friend class Project;
		friend class Scene;
		friend class SceneHierarchyPanel;
		friend class SceneSerializer;
		friend class EditorLayer;
		friend class RuntimeLayer;
		friend struct ScriptStorage;
	};

}
