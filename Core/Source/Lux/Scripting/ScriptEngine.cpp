#include "lpch.h"
#include "ScriptEngine.h"
#include "ScriptGlue.h"
#include "ScriptAsset.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/Hash.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Utilities/FileSystem.h"

#include <Coral/HostInstance.hpp>
#include <Coral/StringHelper.hpp>
#include <Coral/Attribute.hpp>
#include <Coral/TypeCache.hpp>
#include <Coral/GC.hpp>

#include <algorithm>
#include <ranges>

namespace Lux {

	static std::unordered_map<std::string, DataType> s_DataTypeLookup = {
		{ "System.SByte", DataType::SByte },
		{ "System.Byte", DataType::Byte },
		{ "System.Int16", DataType::Short },
		{ "System.UInt16", DataType::UShort },
		{ "System.Int32", DataType::Int },
		{ "System.UInt32", DataType::UInt },
		{ "System.Int64", DataType::Long },
		{ "System.UInt64", DataType::ULong },
		{ "System.Single", DataType::Float },
		{ "System.Double", DataType::Double },
		{ "Lux.Vector2", DataType::Vector2 },
		{ "Lux.Vector3", DataType::Vector3 },
		{ "Lux.Vector4", DataType::Vector4 },
		{ "System.Boolean", DataType::Bool },
		{ "Lux.Entity", DataType::Entity },
		{ "Lux.Prefab", DataType::Prefab },
		{ "Lux.Mesh", DataType::Mesh },
		{ "Lux.StaticMesh", DataType::StaticMesh },
		{ "Lux.Material", DataType::Material },
		{ "Lux.Texture2D", DataType::Texture2D },
		{ "Lux.Scene", DataType::Scene },
	};

	static void OnCoralMessage(std::string_view message, Coral::MessageLevel level)
	{
		switch (level)
		{
			case Coral::MessageLevel::Info:    LUX_CORE_INFO("[Scripting] {}", message); break;
			case Coral::MessageLevel::Warning: LUX_CORE_WARN("[Scripting] {}", message); break;
			case Coral::MessageLevel::Error:   LUX_CORE_ERROR("[Scripting] {}", message); break;
			default:                           LUX_CORE_TRACE("[Scripting] {}", message); break;
		}
	}

	static void OnCSharpException(std::string_view message)
	{
		LUX_CORE_ERROR("[Scripting] C# Exception: {}", message);
	}

	const ScriptEngine& ScriptEngine::GetInstance() { return GetMutable(); }

	Ref<Scene> ScriptEngine::GetCurrentScene() const { return m_CurrentScene; }
	void ScriptEngine::SetCurrentScene(Ref<Scene> scene) { m_CurrentScene = scene; }

	ScriptEngine& ScriptEngine::GetMutable()
	{
		static ScriptEngine s_Instance;
		return s_Instance;
	}

	void ScriptEngine::InitializeHost()
	{
		m_Host = std::make_unique<Coral::HostInstance>();

		Coral::HostSettings settings =
		{
			.CoralDirectory = std::filesystem::absolute(FileSystem::GetWorkingDirectory() / "DotNet").string(),
			.MessageCallback = OnCoralMessage,
			.ExceptionCallback = OnCSharpException
		};

		Coral::CoralInitStatus initStatus = m_Host->Initialize(settings);
		if (initStatus == Coral::CoralInitStatus::Success)
			return;

		switch (initStatus)
		{
			case Coral::CoralInitStatus::CoralManagedNotFound:
				LUX_CORE_ERROR("[Scripting] Could not find Coral.Managed.dll in '{}'.", settings.CoralDirectory); break;
			case Coral::CoralInitStatus::CoralManagedInitError:
				LUX_CORE_ERROR("[Scripting] Failed to initialize Coral.Managed."); break;
			case Coral::CoralInitStatus::DotNetNotFound:
				LUX_CORE_ERROR("[Scripting] .NET 9 runtime not found. Install it from https://dotnet.microsoft.com/download/dotnet/9.0"); break;
			default: break;
		}

		// Non-fatal: leave the host null so scripting is simply disabled this session.
		m_Host.reset();
	}

	void ScriptEngine::ShutdownHost()
	{
		if (!m_Host)
			return;

		Coral::TypeCache::Get().Clear();
		m_Host->Shutdown();
		m_Host.reset();

		std::error_code ec;
		if (!m_ShadowDirRoot.empty())
			std::filesystem::remove_all(m_ShadowDirRoot, ec);
	}

	void ScriptEngine::Initialize(Ref<Project> project)
	{
		if (!m_Host)
			return;

		m_LoadContext = std::make_unique<Coral::AssemblyLoadContext>(std::move(m_Host->CreateAssemblyLoadContext("LuxScriptRuntime")));

		auto scriptCorePath = std::filesystem::absolute(FileSystem::GetWorkingDirectory() / "Resources" / "Scripts" / "ScriptCore.dll").string();
		m_CoreAssemblyData = CreateScope<AssemblyData>();
		m_CoreAssemblyData->Assembly = &m_LoadContext->LoadAssembly(scriptCorePath);
		if (m_CoreAssemblyData->Assembly->GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
		{
			LUX_CORE_ERROR("[Scripting] Failed to load ScriptCore.dll from '{}'.", scriptCorePath);
			return;
		}

		BuildAssemblyCache(m_CoreAssemblyData.get());

		ScriptGlue::RegisterGlue(*m_CoreAssemblyData->Assembly);
		// The app assembly is loaded by the caller (LoadProjectAssembly for the editor,
		// LoadProjectAssemblyRuntime for a packed runtime build).
	}

	void ScriptEngine::Shutdown()
	{
		m_ManagedObjects.Clear();

		for (auto& [scriptID, scriptMetadata] : m_ScriptMetadata)
		{
			for (auto& [fieldID, fieldMetadata] : scriptMetadata.Fields)
				fieldMetadata.DefaultValue.Release();
		}
		m_ScriptMetadata.clear();

		m_AppAssemblyData.reset();
		m_CoreAssemblyData.reset();

		if (m_Host && m_LoadContext)
			m_Host->UnloadAssemblyLoadContext(*m_LoadContext);
		m_LoadContext.reset();

		Coral::TypeCache::Get().Clear();
		m_CurrentScene = nullptr;
	}

	std::filesystem::path ScriptEngine::ShadowCopyAssembly(const std::filesystem::path& originalAbsolute)
	{
		std::error_code ec;
		if (m_ShadowDirRoot.empty())
			m_ShadowDirRoot = std::filesystem::temp_directory_path() / "LuxScriptShadow";

		std::filesystem::path shadowDir = m_ShadowDirRoot / std::to_string(m_ShadowCounter++);
		std::filesystem::create_directories(shadowDir, ec);
		if (ec)
			return originalAbsolute;

		std::filesystem::path stem = originalAbsolute;
		stem.replace_extension();
		const char* sidecars[] = { ".dll", ".pdb", ".deps.json", ".runtimeconfig.json" };
		for (const char* ext : sidecars)
		{
			std::filesystem::path src = stem;
			src += ext;
			if (!std::filesystem::exists(src, ec))
				continue;
			std::filesystem::copy_file(src, shadowDir / src.filename(), std::filesystem::copy_options::overwrite_existing, ec);
			if (ec && std::string(ext) == ".dll")
				return originalAbsolute;
		}

		return shadowDir / originalAbsolute.filename();
	}

	void ScriptEngine::LoadProjectAssembly()
	{
		m_AppAssemblyData.reset();

		auto original = std::filesystem::absolute(Project::GetActiveScriptModuleFilePath());
		if (!std::filesystem::exists(original))
		{
			LUX_CORE_WARN("[Scripting] Script module not found at '{}'. Build the script project to enable scripting.", original.string());
			return;
		}

		// Shadow-copy so the original stays unlocked for rebuilds and the PDB stays associated.
		auto loadPath = ShadowCopyAssembly(original);

		m_AppAssemblyData = CreateScope<AssemblyData>();
		m_AppAssemblyData->Assembly = &m_LoadContext->LoadAssembly(loadPath.string());

		if (m_AppAssemblyData->Assembly->GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
		{
			LUX_CORE_ERROR("[Scripting] Failed to load app assembly '{}'.", loadPath.string());
			m_AppAssemblyData.reset();
			return;
		}

		BuildAssemblyCache(m_AppAssemblyData.get());
	}

	void ScriptEngine::LoadProjectAssemblyRuntime(Buffer data)
	{
		m_AppAssemblyData.reset();
		m_AppAssemblyData = CreateScope<AssemblyData>();

		// An assembly loaded from a byte[] has an empty Assembly.Location, and Coral enumerates
		// local types by location - so LoadAssemblyFromMemory yields zero types, BuildAssemblyCache
		// caches nothing, and every script silently fails to register. Stage the packed binary on
		// disk and use the same path-based load the editor uses (which does report its types).
		std::error_code ec;
		if (m_ShadowDirRoot.empty())
			m_ShadowDirRoot = std::filesystem::temp_directory_path() / "LuxScriptShadow";

		const std::filesystem::path stagedDir = m_ShadowDirRoot / std::to_string(m_ShadowCounter++);
		std::filesystem::create_directories(stagedDir, ec);

		bool staged = false;
		const std::filesystem::path stagedAssembly = stagedDir / "App.dll";
		if (!ec)
		{
			std::ofstream out(stagedAssembly, std::ios::binary | std::ios::trunc);
			if (out.is_open())
			{
				out.write(reinterpret_cast<const char*>(data.Data), (std::streamsize)data.Size);
				staged = out.good();
			}
		}

		if (staged)
		{
			m_AppAssemblyData->Assembly = &m_LoadContext->LoadAssembly(stagedAssembly.string());
		}
		else
		{
			LUX_CORE_WARN("[Scripting] Could not stage the packed app assembly to disk; falling back to an in-memory load, "
				"which may not expose any script types.");
			m_AppAssemblyData->Assembly = &m_LoadContext->LoadAssemblyFromMemory(reinterpret_cast<const std::byte*>(data.Data), (int64_t)data.Size);
		}

		if (m_AppAssemblyData->Assembly->GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
		{
			LUX_CORE_ERROR("[Scripting] Failed to load packed app assembly.");
			m_AppAssemblyData.reset();
			return;
		}

		BuildAssemblyCache(m_AppAssemblyData.get());
	}

	void ScriptEngine::ReloadAppAssembly()
	{
		if (!m_Host || !m_LoadContext)
			return;

		// Drop all live handles + metadata before unloading the context; any survivor pins it.
		m_ManagedObjects.Clear();

		for (auto& [scriptID, scriptMetadata] : m_ScriptMetadata)
		{
			for (auto& [fieldID, fieldMetadata] : scriptMetadata.Fields)
				fieldMetadata.DefaultValue.Release();
		}
		m_ScriptMetadata.clear();

		m_AppAssemblyData.reset();
		m_CoreAssemblyData.reset();

		m_Host->UnloadAssemblyLoadContext(*m_LoadContext);
		Coral::TypeCache::Get().Clear();
		Coral::GC::Collect();

		m_LoadContext = std::make_unique<Coral::AssemblyLoadContext>(std::move(m_Host->CreateAssemblyLoadContext("LuxScriptRuntime")));

		auto scriptCorePath = std::filesystem::absolute(FileSystem::GetWorkingDirectory() / "Resources" / "Scripts" / "ScriptCore.dll").string();
		m_CoreAssemblyData = CreateScope<AssemblyData>();
		m_CoreAssemblyData->Assembly = &m_LoadContext->LoadAssembly(scriptCorePath);
		BuildAssemblyCache(m_CoreAssemblyData.get());

		ScriptGlue::RegisterGlue(*m_CoreAssemblyData->Assembly);

		LoadProjectAssembly();
	}

	bool ScriptEngine::IsValidScript(UUID scriptID) const
	{
		if (!m_AppAssemblyData)
			return false;

		return m_AppAssemblyData->CachedTypes.contains(scriptID) && m_ScriptMetadata.contains(scriptID);
	}

	const ScriptMetadata& ScriptEngine::GetScriptMetadata(UUID scriptID) const
	{
		LUX_CORE_VERIFY(m_ScriptMetadata.contains(scriptID));
		return m_ScriptMetadata.at(scriptID);
	}

	const Coral::Type* ScriptEngine::GetTypeByName(std::string_view name) const
	{
		if (m_CoreAssemblyData)
		{
			for (const auto& [id, type] : m_CoreAssemblyData->CachedTypes)
			{
				Coral::ScopedString typeName = type->GetFullName();
				if (typeName == name)
					return type;
			}
		}

		if (m_AppAssemblyData)
		{
			for (const auto& [id, type] : m_AppAssemblyData->CachedTypes)
			{
				Coral::ScopedString typeName = type->GetFullName();
				if (typeName == name)
					return type;
			}
		}

		return nullptr;
	}

	void ScriptEngine::BuildAssemblyCache(AssemblyData* assemblyData)
	{
		const std::vector<Coral::Type>& types = assemblyData->Assembly->GetLocalTypes();

		// Lux.Entity lives in ScriptCore, so resolve the base type from the CORE assembly, not
		// the assembly being cached (the app assembly has no local Lux.Entity). Both share one
		// ALC, so cross-assembly IsSubclassOf works.
		Coral::Type& entityType = m_CoreAssemblyData->Assembly->GetLocalType("Lux.Entity");
		if (!entityType)
			LUX_CORE_ERROR("[Scripting] Could not resolve 'Lux.Entity' in ScriptCore - no script will be registered.");

		uint32_t scriptCount = 0;

		for (const Coral::Type& constType : types)
		{
			Coral::Type& type = const_cast<Coral::Type&>(constType);

			Coral::ScopedString fullNameScoped = type.GetFullName();
			std::string fullName = fullNameScoped;
			UUID scriptID = Hash::GenerateFNVHash(fullName);

			assemblyData->CachedTypes[scriptID] = &type;

			if (!entityType || !type.IsSubclassOf(entityType))
			{
				// Only interesting for the app assembly - ScriptCore is nearly all non-script types.
				// A game type landing here usually means a duplicate ScriptCore was resolved, giving
				// two distinct Lux.Entity identities; the script would otherwise vanish silently.
				if (assemblyData != m_CoreAssemblyData.get())
				{
					LUX_CORE_TRACE("[Scripting] Type '{}' (ID {}) is not a Lux.Entity subclass; not registered as a script.",
						fullName, (uint64_t)scriptID);
				}
				continue;
			}

			scriptCount++;

			auto& metadata = m_ScriptMetadata[scriptID];
			metadata.FullName = fullName;

			for (auto& methodInfo : type.GetMethods())
			{
				Coral::ScopedString methodName = methodInfo.GetName();
				metadata.Methods.insert(std::string(methodName));
			}

			auto temp = type.CreateInstance();

			for (auto& fieldInfo : type.GetFields())
			{
				Coral::ScopedString fieldNameScoped = fieldInfo.GetName();
				std::string fieldNameStr = fieldNameScoped;

				if (fieldNameStr.find("k__BackingField") != std::string::npos)
					continue;

				auto* fieldType = &fieldInfo.GetType();
				if (fieldType->IsSZArray())
					fieldType = &fieldType->GetElementType();

				Coral::ScopedString typeNameScoped = fieldType->GetFullName();
				std::string typeName = typeNameScoped;

				if (!s_DataTypeLookup.contains(typeName))
					continue;

				if (fieldInfo.GetAccessibility() != Coral::TypeAccessibility::Public)
				{
					auto attributes = fieldInfo.GetAttributes();
					auto found = std::ranges::find_if(attributes, [](Coral::Attribute& attribute)
					{
						Coral::ScopedString name = attribute.GetType().GetFullName();
						return name == "Lux.ShowInEditorAttribute";
					});

					if (found == attributes.end())
						continue;
				}

				// Entity.ID is inherited by every script; not an editable field.
				if (fieldNameStr == "ID")
					continue;

				std::string fullFieldName = std::format("{}.{}", fullName, fieldNameStr);
				uint32_t fieldID = Hash::GenerateFNVHash(fullFieldName);

				auto& fieldMetadata = metadata.Fields[fieldID];
				fieldMetadata.Name = fieldNameStr;
				fieldMetadata.Type = s_DataTypeLookup.at(typeName);
				fieldMetadata.ManagedType = &fieldInfo.GetType();

				switch (fieldMetadata.Type)
				{
					case DataType::SByte:   fieldMetadata.SetDefaultValue<int8_t>(temp); break;
					case DataType::Byte:    fieldMetadata.SetDefaultValue<uint8_t>(temp); break;
					case DataType::Short:   fieldMetadata.SetDefaultValue<int16_t>(temp); break;
					case DataType::UShort:  fieldMetadata.SetDefaultValue<uint16_t>(temp); break;
					case DataType::Int:     fieldMetadata.SetDefaultValue<int32_t>(temp); break;
					case DataType::UInt:    fieldMetadata.SetDefaultValue<uint32_t>(temp); break;
					case DataType::Long:    fieldMetadata.SetDefaultValue<int64_t>(temp); break;
					case DataType::ULong:   fieldMetadata.SetDefaultValue<uint64_t>(temp); break;
					case DataType::Float:   fieldMetadata.SetDefaultValue<float>(temp); break;
					case DataType::Double:  fieldMetadata.SetDefaultValue<double>(temp); break;
					case DataType::Vector2: fieldMetadata.SetDefaultValue<glm::vec2>(temp); break;
					case DataType::Vector3: fieldMetadata.SetDefaultValue<glm::vec3>(temp); break;
					case DataType::Vector4: fieldMetadata.SetDefaultValue<glm::vec4>(temp); break;
					default: break; // Bool + asset-refs default to zero-initialized storage.
				}
			}

			temp.Destroy();
		}

		LUX_CORE_INFO("[Scripting] Cached {} type(s), {} registered as scripts (Lux.Entity subclasses).",
			types.size(), scriptCount);
	}

}
