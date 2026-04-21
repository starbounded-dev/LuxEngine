#include "lpch.h"
#include "Lux/Asset/AssetManager/EditorAssetManager.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Utilities/FileSystem.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <yaml-cpp/yaml.h>

namespace Lux
{
	namespace
	{
		AssetMetadata s_NullMetadata;

		std::string AssetStatusToString(AssetStatus status)
		{
			switch (status)
			{
			case AssetStatus::None:    return "None";
			case AssetStatus::Ready:   return "Ready";
			case AssetStatus::Invalid: return "Invalid";
			case AssetStatus::Loading: return "Loading";
			case AssetStatus::Missing: return "Missing";
			}

			return "None";
		}

		AssetStatus AssetStatusFromString(const std::string& status)
		{
			if (status == "Ready")   return AssetStatus::Ready;
			if (status == "Invalid") return AssetStatus::Invalid;
			if (status == "Loading") return AssetStatus::Loading;
			if (status == "Missing") return AssetStatus::Missing;
			return AssetStatus::None;
		}

		template<typename TAsset>
		void RegisterMaterialTableDependencies(EditorAssetManager& manager, const Ref<TAsset>& asset)
		{
			if (!asset)
				return;

			Ref<MaterialTable> materialTable = asset->GetMaterials();
			if (!materialTable)
				return;

			for (const auto& [slot, materialHandle] : materialTable->GetMaterials())
			{
				(void)slot;
				if (materialHandle)
					manager.RegisterDependency(materialHandle, asset->Handle);
			}
		}
	}

	EditorAssetManager::EditorAssetManager()
	{
		m_AssetThread = Ref<EditorAssetSystem>::Create();
		AssetImporter::Init();

		if (Project::GetActive() && !Project::GetActiveProjectDirectory().empty())
		{
			LoadAssetRegistry();
			ReloadAssets();
		}
	}

	EditorAssetManager::~EditorAssetManager()
	{
		Shutdown();
	}

	void EditorAssetManager::Shutdown()
	{
		if (m_AssetThread)
		{
			m_AssetThread->Stop();
			m_AssetThread = nullptr;
		}

		if (Project::GetActive() && !Project::GetActiveProjectDirectory().empty())
			WriteRegistryToFile();
	}

	AssetType EditorAssetManager::GetAssetType(AssetHandle assetHandle)
	{
		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return memoryAsset->GetAssetType();

		AssetMetadata metadata = GetMetadata(assetHandle);
		return metadata.IsValid() ? metadata.Type : AssetType::None;
	}

	Ref<Asset> EditorAssetManager::GetAsset(AssetHandle assetHandle)
	{
		SyncWithAssetThread();

		Ref<Asset> asset = GetAssetIncludingInvalid(assetHandle);
		return asset;
	}

	AsyncAssetResult<Asset> EditorAssetManager::GetAssetAsync(AssetHandle assetHandle)
	{
		SyncWithAssetThread();

		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return { memoryAsset, true };

		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return {};

		if (metadata.IsDataLoaded && m_LoadedAssets.contains(assetHandle))
			return { m_LoadedAssets.at(assetHandle), true };

		if (metadata.Status != AssetStatus::Loading)
		{
			metadata.Status = AssetStatus::Loading;
			SetMetadata(assetHandle, metadata);
			LoadAssetAsync(assetHandle);
		}

		return {};
	}

	void EditorAssetManager::AddMemoryOnlyAsset(Ref<Asset> asset)
	{
		if (!asset)
			return;

		if (!asset->Handle)
			asset->Handle = AssetHandle();

		asset->Flags = (uint16_t)AssetFlag::None;

		std::scoped_lock lock(m_MemoryAssetsMutex);
		m_MemoryAssets[asset->Handle] = asset;
	}

	bool EditorAssetManager::ReloadData(AssetHandle assetHandle)
	{
		if (IsMemoryAsset(assetHandle))
			return GetMemoryAsset(assetHandle) != nullptr;

		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return false;

		m_LoadedAssets.erase(assetHandle);
		DeregisterDependencies(assetHandle);
		metadata.IsDataLoaded = false;
		metadata.Status = AssetStatus::None;
		SetMetadata(assetHandle, metadata);

		Ref<Asset> asset;
		if (!LoadAssetData(metadata, asset))
			return false;

		UpdateDependents(assetHandle);
		return asset != nullptr;
	}

	void EditorAssetManager::ReloadDataAsync(AssetHandle assetHandle)
	{
		if (IsMemoryAsset(assetHandle))
			return;

		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return;

		m_LoadedAssets.erase(assetHandle);
		DeregisterDependencies(assetHandle);
		metadata.IsDataLoaded = false;
		metadata.Status = AssetStatus::Loading;
		SetMetadata(assetHandle, metadata);
		LoadAssetAsync(assetHandle);
	}

	bool EditorAssetManager::EnsureCurrent(AssetHandle assetHandle)
	{
		if (IsMemoryAsset(assetHandle))
			return true;

		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return false;

		if (!FileExists(metadata))
		{
			metadata.Status = AssetStatus::Missing;
			metadata.IsDataLoaded = false;
			m_LoadedAssets.erase(assetHandle);
			SetMetadata(assetHandle, metadata);
			return false;
		}

		uint64_t currentTimestamp = FileSystem::GetLastWriteTime(GetFileSystemPath(metadata));
		if (!metadata.IsDataLoaded || currentTimestamp > metadata.FileLastWriteTime)
			return ReloadData(assetHandle);

		return metadata.Status != AssetStatus::Invalid && metadata.Status != AssetStatus::Missing;
	}

	bool EditorAssetManager::EnsureAllLoadedCurrent()
	{
		bool allCurrent = true;

		std::vector<AssetHandle> handles;
		handles.reserve(m_LoadedAssets.size());
		for (const auto& [handle, asset] : m_LoadedAssets)
		{
			(void)asset;
			handles.push_back(handle);
		}

		for (AssetHandle handle : handles)
			allCurrent &= EnsureCurrent(handle);

		return allCurrent;
	}

	bool EditorAssetManager::IsAssetHandleValid(AssetHandle assetHandle)
	{
		return GetMemoryAsset(assetHandle) != nullptr || GetMetadata(assetHandle).IsValid();
	}

	Ref<Asset> EditorAssetManager::GetMemoryAsset(AssetHandle handle)
	{
		std::shared_lock lock(m_MemoryAssetsMutex);
		auto it = m_MemoryAssets.find(handle);
		return it != m_MemoryAssets.end() ? it->second : nullptr;
	}

	bool EditorAssetManager::IsAssetLoaded(AssetHandle handle)
	{
		if (IsMemoryAsset(handle))
			return true;

		AssetMetadata metadata = GetMetadata(handle);
		return metadata.IsValid() && metadata.IsDataLoaded && m_LoadedAssets.contains(handle);
	}

	bool EditorAssetManager::IsAssetValid(AssetHandle handle)
	{
		if (IsMemoryAsset(handle))
			return true;

		AssetMetadata metadata = GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		if (!metadata.IsDataLoaded)
			return GetAsset(handle) != nullptr;

		return metadata.Status == AssetStatus::Ready && m_LoadedAssets.contains(handle);
	}

	bool EditorAssetManager::IsAssetMissing(AssetHandle handle)
	{
		if (IsMemoryAsset(handle))
			return false;

		AssetMetadata metadata = GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		if (!FileExists(metadata))
		{
			SetMetadata(handle, metadata);
			m_LoadedAssets.erase(handle);
			return true;
		}

		return false;
	}

	bool EditorAssetManager::IsMemoryAsset(AssetHandle handle)
	{
		std::shared_lock lock(m_MemoryAssetsMutex);
		return m_MemoryAssets.find(handle) != m_MemoryAssets.end();
	}

	bool EditorAssetManager::IsPhysicalAsset(AssetHandle handle)
	{
		return !IsMemoryAsset(handle) && GetMetadata(handle).IsValid();
	}

	void EditorAssetManager::RemoveAsset(AssetHandle handle)
	{
		{
			std::scoped_lock lock(m_MemoryAssetsMutex);
			m_MemoryAssets.erase(handle);
		}

		m_LoadedAssets.erase(handle);
		DeregisterDependencies(handle);

		{
			std::unique_lock lock(m_AssetRegistryMutex);
			m_AssetRegistry.Remove(handle);
		}

		WriteRegistryToFile();
	}

	void EditorAssetManager::RegisterDependency(AssetHandle dependency, AssetHandle handle)
	{
		if (!dependency || !handle || dependency == handle)
			return;

		std::scoped_lock lock(m_AssetDependenciesMutex);
		m_AssetDependencies[handle].insert(dependency);
		m_AssetDependents[dependency].insert(handle);
	}

	void EditorAssetManager::DeregisterDependency(AssetHandle dependency, AssetHandle handle)
	{
		std::scoped_lock lock(m_AssetDependenciesMutex);
		if (m_AssetDependencies.contains(handle))
		{
			m_AssetDependencies[handle].erase(dependency);
			if (m_AssetDependencies[handle].empty())
				m_AssetDependencies.erase(handle);
		}

		if (m_AssetDependents.contains(dependency))
		{
			m_AssetDependents[dependency].erase(handle);
			if (m_AssetDependents[dependency].empty())
				m_AssetDependents.erase(dependency);
		}
	}

	void EditorAssetManager::DeregisterDependencies(AssetHandle handle)
	{
		std::scoped_lock lock(m_AssetDependenciesMutex);
		auto it = m_AssetDependencies.find(handle);
		if (it == m_AssetDependencies.end())
			return;

		for (AssetHandle dependency : it->second)
		{
			if (m_AssetDependents.contains(dependency))
			{
				m_AssetDependents[dependency].erase(handle);
				if (m_AssetDependents[dependency].empty())
					m_AssetDependents.erase(dependency);
			}
		}

		m_AssetDependencies.erase(it);
	}

	std::unordered_set<AssetHandle> EditorAssetManager::GetDependencies(AssetHandle handle)
	{
		std::shared_lock lock(m_AssetDependenciesMutex);
		auto it = m_AssetDependencies.find(handle);
		return it != m_AssetDependencies.end() ? it->second : std::unordered_set<AssetHandle>{};
	}

	void EditorAssetManager::SyncWithAssetThread()
	{
		if (!m_AssetThread)
			return;

		std::vector<EditorAssetLoadResponse> loadedAssets;
		m_AssetThread->SyncLoadedAssets(loadedAssets);

		for (auto& response : loadedAssets)
		{
			AssetMetadata metadata = GetMetadata(response.Metadata.Handle);
			if (!metadata.IsValid())
				metadata = response.Metadata;

			if (response.Asset)
			{
				response.Asset->Handle = metadata.Handle;
				response.Asset->Flags = (uint16_t)AssetFlag::None;
				m_LoadedAssets[metadata.Handle] = response.Asset;

				metadata.IsDataLoaded = true;
				metadata.Status = AssetStatus::Ready;
				metadata.FileLastWriteTime = FileSystem::GetLastWriteTime(GetFileSystemPath(metadata));
				SetMetadata(metadata.Handle, metadata);
				RegisterAssetDependencies(response.Asset);
				UpdateDependents(metadata.Handle);
			}
			else
			{
				m_LoadedAssets.erase(metadata.Handle);
				metadata.IsDataLoaded = false;
				metadata.Status = FileSystem::Exists(GetFileSystemPath(metadata)) ? AssetStatus::Invalid : AssetStatus::Missing;
				SetMetadata(metadata.Handle, metadata);
			}
		}
	}

	std::unordered_set<AssetHandle> EditorAssetManager::GetAllAssetsWithType(AssetType type)
	{
		std::unordered_set<AssetHandle> result;

		{
			std::shared_lock lock(m_MemoryAssetsMutex);
			for (const auto& [handle, asset] : m_MemoryAssets)
			{
				if (asset && asset->GetAssetType() == type)
					result.insert(handle);
			}
		}

		std::shared_lock lock(m_AssetRegistryMutex);
		for (const auto& [handle, metadata] : m_AssetRegistry)
		{
			if (metadata.Type == type)
				result.insert(handle);
		}

		return result;
	}

	AssetMetadata EditorAssetManager::GetMetadata(AssetHandle handle) const
	{
		std::shared_lock lock(m_AssetRegistryMutex);
		if (m_AssetRegistry.Contains(handle))
			return m_AssetRegistry.Get(handle);

		return s_NullMetadata;
	}

	void EditorAssetManager::SetMetadata(AssetHandle handle, const AssetMetadata& metadata)
	{
		std::unique_lock lock(m_AssetRegistryMutex);
		m_AssetRegistry.Set(handle, metadata);
	}

	AssetHandle EditorAssetManager::ImportAsset(const std::filesystem::path& filepath)
	{
		std::filesystem::path path = GetRelativePath(filepath);
		if (path.empty())
			return 0;

		AssetType type = GetAssetTypeFromPath(path);
		if (type == AssetType::None)
			return 0;

		if (auto existingHandle = GetAssetHandleFromFilePath(path); existingHandle)
		{
			AssetMetadata metadata = GetMetadata(existingHandle);
			metadata.FilePath = path;
			metadata.Type = type;
			metadata.FileLastWriteTime = FileSystem::GetLastWriteTime(GetFileSystemPath(metadata));
			if (metadata.Status == AssetStatus::Missing)
				metadata.Status = AssetStatus::None;
			SetMetadata(existingHandle, metadata);
			return existingHandle;
		}

		AssetMetadata metadata;
		metadata.Handle = AssetHandle();
		metadata.FilePath = path;
		metadata.Type = type;
		metadata.FileLastWriteTime = FileSystem::GetLastWriteTime(GetFileSystemPath(metadata));
		SetMetadata(metadata.Handle, metadata);
		WriteRegistryToFile();
		return metadata.Handle;
	}

	AssetHandle EditorAssetManager::ImportScriptAsset(const std::filesystem::path& filepath, uint64_t uuid)
	{
		std::filesystem::path path = GetRelativePath(filepath);
		if (path.empty())
			return 0;

		AssetType type = GetAssetTypeFromPath(path);
		if (type == AssetType::None)
			return 0;

		if (auto existingHandle = GetAssetHandleFromFilePath(path); existingHandle)
		{
			AssetMetadata metadata = GetMetadata(existingHandle);
			metadata.FilePath = path;
			metadata.Type = type;
			metadata.FileLastWriteTime = FileSystem::GetLastWriteTime(GetFileSystemPath(metadata));
			if (metadata.Status == AssetStatus::Missing)
				metadata.Status = AssetStatus::None;
			SetMetadata(existingHandle, metadata);
			return existingHandle;
		}

		AssetMetadata metadata;
		metadata.Handle = uuid;
		metadata.FilePath = path;
		metadata.Type = type;
		metadata.FileLastWriteTime = FileSystem::GetLastWriteTime(GetFileSystemPath(metadata));
		SetMetadata(metadata.Handle, metadata);
		WriteRegistryToFile();
		return metadata.Handle;
	}

	AssetHandle EditorAssetManager::GetAssetHandleFromFilePath(const std::filesystem::path& filepath) const
	{
		std::filesystem::path relativePath = GetRelativePath(filepath).lexically_normal();
		std::shared_lock lock(m_AssetRegistryMutex);
		for (const auto& [handle, metadata] : m_AssetRegistry)
		{
			if (metadata.FilePath.lexically_normal() == relativePath)
				return handle;
		}

		return 0;
	}

	AssetType EditorAssetManager::GetAssetTypeFromExtension(const std::string& extension) const
	{
		return AssetExtensions::GetAssetTypeFromExtension(extension);
	}

	std::string EditorAssetManager::GetDefaultExtensionForAssetType(AssetType type) const
	{
		return AssetExtensions::GetDefaultExtensionForAssetType(type);
	}

	AssetType EditorAssetManager::GetAssetTypeFromPath(const std::filesystem::path& path) const
	{
		return AssetExtensions::GetAssetTypeFromPath(path);
	}

	std::filesystem::path EditorAssetManager::GetFileSystemPath(AssetHandle handle) const
	{
		AssetMetadata metadata = GetMetadata(handle);
		return metadata.IsValid() ? GetFileSystemPath(metadata) : std::filesystem::path();
	}

	std::filesystem::path EditorAssetManager::GetFileSystemPath(const AssetMetadata& metadata) const
	{
		if (metadata.FilePath.is_absolute())
			return metadata.FilePath;

		return Project::GetActiveAssetDirectory() / metadata.FilePath;
	}

	std::string EditorAssetManager::GetFileSystemPathString(const AssetMetadata& metadata) const
	{
		return GetFileSystemPath(metadata).string();
	}

	std::filesystem::path EditorAssetManager::GetRelativePath(const std::filesystem::path& filepath) const
	{
		if (filepath.empty())
			return {};

		std::filesystem::path normalizedPath = filepath.lexically_normal();
		std::filesystem::path assetDirectory = Project::GetActiveAssetDirectory().lexically_normal();

		std::string pathString = normalizedPath.generic_string();
		std::string assetDirectoryString = assetDirectory.generic_string();
		if (!assetDirectoryString.empty() && pathString.rfind(assetDirectoryString, 0) == 0)
			return std::filesystem::relative(normalizedPath, assetDirectory).lexically_normal();

		return normalizedPath;
	}

	std::filesystem::path EditorAssetManager::GetFilePath(AssetHandle handle) const
	{
		return GetMetadata(handle).FilePath;
	}

	bool EditorAssetManager::FileExists(AssetMetadata& metadata) const
	{
		if (!metadata.IsValid())
			return false;

		bool exists = FileSystem::Exists(GetFileSystemPath(metadata));
		if (!exists)
		{
			metadata.Status = AssetStatus::Missing;
			metadata.IsDataLoaded = false;
		}

		return exists;
	}

	void EditorAssetManager::LoadAssetAsync(AssetHandle handle)
	{
		if (!m_AssetThread)
			return;

		AssetMetadata metadata = GetMetadata(handle);
		if (!metadata.IsValid() || metadata.IsDataLoaded)
			return;

		m_AssetThread->QueueAssetLoad(metadata);
	}

	void EditorAssetManager::SyncLoadedAssets()
	{
		SyncWithAssetThread();
	}

	void EditorAssetManager::SerializeAssetRegistry()
	{
		WriteRegistryToFile();
	}

	bool EditorAssetManager::DeserializeAssetRegistry()
	{
		LoadAssetRegistry();
		return true;
	}

	Ref<Asset> EditorAssetManager::GetAssetIncludingInvalid(AssetHandle assetHandle)
	{
		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return memoryAsset;

		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return nullptr;

		if (metadata.IsDataLoaded && m_LoadedAssets.contains(assetHandle))
			return m_LoadedAssets.at(assetHandle);

		Ref<Asset> asset;
		if (!LoadAssetData(metadata, asset))
			return nullptr;

		return asset;
	}

	bool EditorAssetManager::LoadAssetData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
		AssetMetadata mutableMetadata = metadata;
		if (!FileExists(mutableMetadata))
		{
			SetMetadata(mutableMetadata.Handle, mutableMetadata);
			return false;
		}

		if (!AssetImporter::TryLoadData(mutableMetadata, asset) || !asset)
		{
			mutableMetadata.Status = AssetStatus::Invalid;
			mutableMetadata.IsDataLoaded = false;
			SetMetadata(mutableMetadata.Handle, mutableMetadata);
			return false;
		}

		asset->Handle = mutableMetadata.Handle;
		asset->Flags = (uint16_t)AssetFlag::None;
		m_LoadedAssets[mutableMetadata.Handle] = asset;

		mutableMetadata.Status = AssetStatus::Ready;
		mutableMetadata.IsDataLoaded = true;
		mutableMetadata.FileLastWriteTime = FileSystem::GetLastWriteTime(GetFileSystemPath(mutableMetadata));
		SetMetadata(mutableMetadata.Handle, mutableMetadata);
		RegisterAssetDependencies(asset);
		return true;
	}

	void EditorAssetManager::LoadAssetRegistry()
	{
		std::unique_lock lock(m_AssetRegistryMutex);
		m_AssetRegistry.Clear();
		lock.unlock();

		std::filesystem::path assetRegistryPath = Project::GetActiveAssetRegistryPath();
		if (!FileSystem::Exists(assetRegistryPath))
			return;

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(assetRegistryPath.string());
		}
		catch (const YAML::ParserException& e)
		{
			LUX_CORE_ERROR("Failed to load asset registry '{}': {}", assetRegistryPath.string(), e.what());
			return;
		}

		YAML::Node assetsNode = data["Assets"];
		if (!assetsNode)
			assetsNode = data["AssetRegistry"];
		if (!assetsNode)
			return;

		for (const auto& node : assetsNode)
		{
			AssetMetadata metadata;
			metadata.Handle = node["Handle"].as<uint64_t>();
			metadata.FilePath = node["FilePath"].as<std::string>();
			metadata.Type = AssetTypeFromString(node["Type"].as<std::string>());
			if (node["FileLastWriteTime"])
				metadata.FileLastWriteTime = node["FileLastWriteTime"].as<uint64_t>();
			if (node["Status"])
				metadata.Status = AssetStatusFromString(node["Status"].as<std::string>());

			metadata.IsDataLoaded = false;
			SetMetadata(metadata.Handle, metadata);
		}
	}

	void EditorAssetManager::ProcessDirectory(const std::filesystem::path& directoryPath)
	{
		if (!FileSystem::Exists(directoryPath) || !FileSystem::IsDirectory(directoryPath))
			return;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath))
		{
			if (!entry.is_regular_file())
				continue;

			ImportAsset(entry.path());
		}
	}

	void EditorAssetManager::ReloadAssets()
	{
		std::filesystem::path assetDirectory = Project::GetActiveAssetDirectory();
		if (!FileSystem::Exists(assetDirectory))
			return;

		ProcessDirectory(assetDirectory);
		WriteRegistryToFile();
	}

	void EditorAssetManager::WriteRegistryToFile()
	{
		if (!Project::GetActive())
			return;

		std::filesystem::path assetRegistryPath = Project::GetActiveAssetRegistryPath();
		if (assetRegistryPath.empty())
			return;

		std::filesystem::create_directories(assetRegistryPath.parent_path());

		struct AssetRegistryEntry
		{
			std::string FilePath;
			AssetType Type = AssetType::None;
			uint64_t FileLastWriteTime = 0;
			AssetStatus Status = AssetStatus::None;
		};

		std::map<uint64_t, AssetRegistryEntry> sortedEntries;
		{
			std::shared_lock lock(m_AssetRegistryMutex);
			for (const auto& [handle, metadata] : m_AssetRegistry)
			{
				sortedEntries[(uint64_t)handle] = {
					metadata.FilePath.generic_string(),
					metadata.Type,
					metadata.FileLastWriteTime,
					metadata.Status
				};
			}
		}

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Assets" << YAML::Value << YAML::BeginSeq;
		for (const auto& [handle, entry] : sortedEntries)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Handle" << YAML::Value << handle;
			out << YAML::Key << "FilePath" << YAML::Value << entry.FilePath;
			out << YAML::Key << "Type" << YAML::Value << std::string(AssetTypeToString(entry.Type));
			out << YAML::Key << "FileLastWriteTime" << YAML::Value << entry.FileLastWriteTime;
			out << YAML::Key << "Status" << YAML::Value << AssetStatusToString(entry.Status);
			out << YAML::EndMap;
		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(assetRegistryPath);
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("Failed to write asset registry '{}'", assetRegistryPath.string());
			return;
		}

		fout << out.c_str();
	}

	void EditorAssetManager::RegisterAssetDependencies(const Ref<Asset>& asset)
	{
		if (!asset)
			return;

		DeregisterDependencies(asset->Handle);

		if (auto materialAsset = asset.As<MaterialAsset>())
		{
			if (materialAsset->GetAlbedoMapHandle())
				RegisterDependency(materialAsset->GetAlbedoMapHandle(), materialAsset->Handle);
			if (materialAsset->GetNormalMapHandle())
				RegisterDependency(materialAsset->GetNormalMapHandle(), materialAsset->Handle);
			if (materialAsset->GetMetalnessMapHandle())
				RegisterDependency(materialAsset->GetMetalnessMapHandle(), materialAsset->Handle);
			if (materialAsset->GetRoughnessMapHandle())
				RegisterDependency(materialAsset->GetRoughnessMapHandle(), materialAsset->Handle);
			return;
		}

		if (auto mesh = asset.As<Mesh>())
		{
			if (mesh->GetMeshSource())
				RegisterDependency(mesh->GetMeshSource(), mesh->Handle);
			RegisterMaterialTableDependencies(*this, mesh);
			return;
		}

		if (auto staticMesh = asset.As<StaticMesh>())
		{
			if (staticMesh->GetMeshSource())
				RegisterDependency(staticMesh->GetMeshSource(), staticMesh->Handle);
			RegisterMaterialTableDependencies(*this, staticMesh);
		}
	}

	void EditorAssetManager::UpdateDependents(AssetHandle handle)
	{
		std::unordered_set<AssetHandle> dependents;
		{
			std::shared_lock lock(m_AssetDependenciesMutex);
			auto it = m_AssetDependents.find(handle);
			if (it != m_AssetDependents.end())
				dependents = it->second;
		}

		for (AssetHandle dependentHandle : dependents)
		{
			if (!IsAssetLoaded(dependentHandle))
				continue;

			Ref<Asset> dependentAsset = IsMemoryAsset(dependentHandle) ? GetMemoryAsset(dependentHandle) : m_LoadedAssets.at(dependentHandle);
			if (dependentAsset)
				dependentAsset->OnDependencyUpdated(handle);
		}
	}
}
