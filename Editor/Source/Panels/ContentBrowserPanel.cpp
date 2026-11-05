// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioSurfaceTable.h"
#include "Lux/Audio/DialogueTable.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Audio/SoundBank.h"
#include <fstream>
#include "ContentBrowserPanel.h"
#include "ScriptTemplate.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetExtensions.h"
#ifndef LUX_DIST
#include "Lux/Asset/AssimpMeshImporter.h"
#endif
#include "Lux/Core/Application.h"
#include "Lux/Core/Input.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Asset/PrefabSerializer.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

namespace Lux {

	namespace
	{
		const std::set<AssetType> s_SupportedThumbnailAssetTypes = {
			AssetType::Texture,
			AssetType::Material,
			AssetType::Mesh,
			AssetType::MeshSource,
			AssetType::StaticMesh,
			AssetType::EnvMap,
			AssetType::Audio,
			AssetType::Sound
		};

		bool IsThumbnailSupported(AssetType type)
		{
			return s_SupportedThumbnailAssetTypes.contains(type);
		}

		std::string ToLowerExtension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return extension;
		}

		void CopyDependencyFile(const std::filesystem::path& sourceFile, const std::filesystem::path& sourceRoot, const std::filesystem::path& destinationRoot)
		{
			if (sourceFile.empty() || !std::filesystem::exists(sourceFile) || !std::filesystem::is_regular_file(sourceFile))
				return;

			std::error_code ec;
			std::filesystem::path relativePath = std::filesystem::relative(sourceFile, sourceRoot, ec);
			const std::string relativePathString = relativePath.generic_string();
			if (ec || relativePath.empty() || relativePathString.rfind("..", 0) == 0)
				relativePath = sourceFile.filename();

			const std::filesystem::path destinationFile = destinationRoot / relativePath;
			std::filesystem::create_directories(destinationFile.parent_path(), ec);
			std::filesystem::copy_file(sourceFile, destinationFile, std::filesystem::copy_options::skip_existing, ec);
		}

		void CopyImportedMeshDependencies(const std::filesystem::path& sourceFile, const std::filesystem::path& destinationDirectory)
		{
#ifndef LUX_DIST
			if (!Project::GetEditorAssetManager() || Project::GetEditorAssetManager()->GetAssetTypeFromPath(sourceFile) != AssetType::MeshSource)
				return;

			const std::filesystem::path sourceRoot = sourceFile.parent_path();

			for (const std::filesystem::path& texturePath : AssimpMeshImporter::GetReferencedTexturePaths(sourceFile))
				CopyDependencyFile(texturePath, sourceRoot, destinationDirectory);

			if (ToLowerExtension(sourceFile) == ".gltf")
			{
				std::error_code ec;
				for (const auto& entry : std::filesystem::directory_iterator(sourceRoot, ec))
				{
					if (ec)
						break;

					if (entry.is_regular_file(ec) && ToLowerExtension(entry.path()) == ".bin")
						CopyDependencyFile(entry.path(), sourceRoot, destinationDirectory);
				}
			}
#endif
		}

		bool PathStartsWith(const std::filesystem::path& path, const std::filesystem::path& prefix)
		{
			auto pathIt = path.begin();
			auto prefixIt = prefix.begin();

			for (; prefixIt != prefix.end(); ++prefixIt, ++pathIt)
			{
				if (pathIt == path.end() || *pathIt != *prefixIt)
					return false;
			}

			return true;
		}

		std::filesystem::path ReplacePathPrefix(const std::filesystem::path& path, const std::filesystem::path& oldPrefix, const std::filesystem::path& newPrefix)
		{
			std::error_code ec;
			std::filesystem::path suffix = std::filesystem::relative(path, oldPrefix, ec);
			if (ec)
				return path;

			return (newPrefix / suffix).lexically_normal();
		}

		std::filesystem::path MakeUniqueDuplicatePath(const std::filesystem::path& filepath)
		{
			std::filesystem::path basePath = filepath;
			if (std::filesystem::exists(basePath))
				return FileSystem::GetUniqueFileName(basePath);
			return basePath;
		}
	}

	ContentBrowserPanel* ContentBrowserPanel::s_Instance = nullptr;

	ContentBrowserPanel::ContentBrowserPanel()
	{
		s_Instance = this;

		m_AssetIconMap[".fbx"] = EditorResources::FBXFileIcon;
		m_AssetIconMap[".obj"] = EditorResources::OBJFileIcon;
		m_AssetIconMap[".gltf"] = EditorResources::GLTFFileIcon;
		m_AssetIconMap[".glb"] = EditorResources::GLBFileIcon;
		m_AssetIconMap[".wav"] = EditorResources::WAVFileIcon;
		m_AssetIconMap[".ogg"] = EditorResources::OGGFileIcon;
		m_AssetIconMap[".cs"] = EditorResources::CSFileIcon;
		m_AssetIconMap[".png"] = EditorResources::PNGFileIcon;
		m_AssetIconMap[".jpg"] = EditorResources::JPGFileIcon;
		m_AssetIconMap[".jpeg"] = EditorResources::JPGFileIcon;
		m_AssetIconMap[".lmat"] = EditorResources::MaterialFileIcon;
		m_AssetIconMap[".luxscene"] = EditorResources::SceneFileIcon;
		m_AssetIconMap[".lmesh"] = EditorResources::MeshFileIcon;
		m_AssetIconMap[".lsmesh"] = EditorResources::StaticMeshFileIcon;

		LoadSettings();
		memset(m_SearchBuffer, 0, MAX_INPUT_BUFFER_LENGTH);
	}

	// Toolbar type-filter chips: index 0 is "All"; see AssetMatchesFilter for the type mapping.
	static const char* s_FilterLabels[] = { "All", "Meshes", "Materials", "Textures", "Scripts", "Scenes", "Prefabs" };

	void ContentBrowserPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		m_IsAnyItemHovered = false;
		m_IsContentBrowserHovered = false;
		m_IsContentBrowserFocused = false;

		if (!ImGui::Begin("Content Browser", &isOpen, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}

		m_IsContentBrowserHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		m_IsContentBrowserFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		RenderNewScriptDialogue();

		if (!m_Project || !m_BaseDirectory)
		{
			ImGuiEx::PanelEmptyState("##browser_project", "No active project",
									 "Open a project to browse its assets.");
			ImGui::End();
			return;
		}

		RenderTopBar();
		ImGui::Separator();
		const auto split =
			ImGuiEx::PanelSplitWidths(ImGui::GetContentRegionAvail().x, m_FolderFraction,
									  ImGui::GetFontSize() * 10.0f, ImGui::GetFontSize() * 15.0f);
		const bool folders = m_ShowFolders && split.SideBySide;
		if (folders)
		{
			ImGui::BeginChild("##content_browser_folders", {split.First, 0.0f});
			RenderFolders();
			ImGui::EndChild();
			ImGui::SameLine(0.0f, 0.0f);
			ImGuiEx::PanelSplitHandle("##folders_split", split, m_FolderFraction);
			ImGui::SameLine(0.0f, 0.0f);
		}
		ImGui::BeginChild("##content_browser_main", {folders ? split.Second : 0.0f, 0.0f});
		{
			const float footerHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
			const float itemsHeight =
				std::max(ImGui::GetFontSize(),
						 ImGui::GetContentRegionAvail().y - footerHeight - ImGui::GetStyle().ItemSpacing.y);
			ImGui::BeginChild("##content_browser_items", {0.0f, itemsHeight});
			{
					if (ImGui::BeginPopupContextWindow("##ContentBrowserBackgroundContext", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
					{
						if (ImGui::BeginMenu("New"))
						{
							if (ImGui::MenuItem("Folder"))
								CreateNewFolder();

							if (ImGui::MenuItem("C# Script..."))
								BeginNewScript();

							if (ImGui::MenuItem("Scene"))
								CreateSceneAsset();

							if (ImGui::MenuItem("Material"))
								CreateMaterialAsset();
							if (ImGui::MenuItem("Audio Surface Table"))
								CreateAudioSurfaceTable();
							if (ImGui::MenuItem("Dialogue Table"))
								CreateDialogueTable();
							if (ImGui::MenuItem("Sound"))
								CreateSoundAsset();
							if (ImGui::MenuItem("Sound Bank"))
								CreateSoundBankAsset();

							ImGui::EndMenu();
						}

						if (ImGui::MenuItem("Import"))
						{
							std::filesystem::path filepath = FileSystem::OpenFileDialog();
							if (!filepath.empty())
							{
								const std::filesystem::path destinationDirectory = Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath;
								if (FileSystem::CopyFile(filepath, destinationDirectory))
								{
									CopyImportedMeshDependencies(filepath, destinationDirectory);
									Refresh();
								}
							}
						}

						if (ImGui::MenuItem("Refresh"))
							Refresh();

						if (ImGui::MenuItem("Copy", "Ctrl+C", nullptr, GetSelectionCount() > 0))
							m_CopiedAssets.CopyFrom(m_SelectedItems);

						if (ImGui::MenuItem("Paste", "Ctrl+V", nullptr, m_CopiedAssets.SelectionCount() > 0))
							PasteCopiedAssets();

						if (ImGui::MenuItem("Duplicate", "Ctrl+D", nullptr, GetSelectionCount() > 0))
						{
							m_CopiedAssets.CopyFrom(m_SelectedItems);
							PasteCopiedAssets();
						}

						ImGui::Separator();

						if (ImGui::MenuItem("Show in Explorer"))
							FileSystem::OpenDirectoryInExplorer(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath);

						ImGui::EndPopup();
					}

					const float paddingForOutline = 2.0f;
					const float scrollBarOffset = 20.0f + ImGui::GetStyle().ScrollbarSize;
					const float panelWidth = ImGui::GetContentRegionAvail().x - scrollBarOffset;
					const float cellSize = GetThumbnailSize() + 2.0f + paddingForOutline;
					int columnCount = IsListView() ? 1 : static_cast<int>(panelWidth / cellSize);
					if (columnCount < 1)
						columnCount = 1;

					ImGui::Columns(columnCount, nullptr, false);
					RenderItems();
					ImGui::Columns(1);

					if (ImGui::IsWindowFocused() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
						UpdateInput();

					RenderDeleteDialogue();
				}
				ImGui::EndChild();

				RenderBottomBar(footerHeight);
		}
		ImGui::EndChild();

		ImGui::End();

		if (m_ThumbnailCache && !s_ThumbnailWorkPaused)
		{
			m_ThumbnailCache->OnUpdate();
			if (m_MaterialThumbnailer)
				m_MaterialThumbnailer->OnUpdate(*m_ThumbnailCache);
			if (m_WaveformThumbnailer)
				m_WaveformThumbnailer->OnUpdate(*m_ThumbnailCache);
		}
	}

	void ContentBrowserPanel::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event) { return OnKeyPressedEvent(event); });
		dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& event) { return OnMouseButtonPressed(event); });
		dispatcher.Dispatch<WindowDropEvent>([this](WindowDropEvent& event) { return OnFilesDropped(event); });
	}

	// Files dropped on the editor window are copied into the open folder and imported, like Import.
	bool ContentBrowserPanel::OnFilesDropped(WindowDropEvent& e)
	{
		if (!m_Project || !m_CurrentDirectory || !Project::GetEditorAssetManager())
			return false;
		const std::filesystem::path destinationDirectory = Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath;
		size_t imported = 0;
		for (const auto& source : e.GetPaths())
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(source, error))
			{
				LUX_CORE_WARN_TAG("Editor", "Not imported: '{}' is not a file", source.string());
				continue;
			}
			if (Project::GetEditorAssetManager()->GetAssetTypeFromPath(source) == AssetType::None)
			{
				LUX_CORE_WARN_TAG("Editor", "Not imported: '{}' is not a type of file the engine uses", source.filename().string());
				continue;
			}
			const std::filesystem::path destination = MakeUniqueDuplicatePath(destinationDirectory / source.filename());
			if (!FileSystem::Copy(source, destination))
			{
				LUX_CORE_ERROR_TAG("Editor", "Could not copy '{}' into the project", source.string());
				continue;
			}
			CopyImportedMeshDependencies(source, destinationDirectory);
			if (Project::GetEditorAssetManager()->ImportAsset(destination))
				++imported;
		}
		if (imported == 0)
			return false;
		LUX_CORE_INFO_TAG("Editor", "Imported {} dropped file(s) into '{}'", imported, m_CurrentDirectory->FilePath.generic_string());
		Refresh();
		return true;
	}

	void ContentBrowserPanel::OnProjectChanged(const Ref<Project>& project)
	{
		m_CopiedAssets.Clear();
		m_OpenDeletePopup = false;
		m_FocusSearchWidget = false;
		m_IsContentBrowserFocused = false;
		m_IsContentBrowserHovered = false;
		m_NewScriptDialogOpen = false;
		m_OpenNewScriptPopup = false;
		m_Project = project;
		m_CurrentItems.Clear();
		m_StatusText.clear();
		m_Directories.clear();
		m_BaseDirectory.reset();
		m_CurrentDirectory.reset();
		m_NextDirectory.reset();
		m_PreviousDirectory.reset();
		m_ThumbnailCache.reset();
		m_MaterialThumbnailer.reset();
		m_WaveformThumbnailer.reset();
		m_BreadCrumbData.clear();
		m_FolderRoots.clear();
		m_LocationLabel.clear();
		ClearSelections();
		memset(m_SearchBuffer, 0, MAX_INPUT_BUFFER_LENGTH);

		if (!project)
			return;

		m_ThumbnailCache = Ref<ThumbnailCache>::Create(project);

		AssetHandle baseDirectoryHandle = ProcessDirectory(project->GetAssetDirectory(), nullptr);
		m_BaseDirectory = m_Directories[baseDirectoryHandle];
		m_FolderRoots.clear();
		for (const auto& [handle, directory] : m_BaseDirectory->SubDirectories)
			m_FolderRoots.push_back(directory);
		std::ranges::sort(m_FolderRoots, {}, [](const Ref<DirectoryInfo>& directory)
						  { return Utils::String::ToLowerCopy(Utils::PathToUTF8(directory->FilePath)); });
		ChangeDirectory(m_BaseDirectory);
	}

	AssetHandle ContentBrowserPanel::ProcessDirectory(const std::filesystem::path& directoryPath, const Ref<DirectoryInfo>& parent)
	{
		if (Ref<DirectoryInfo> existing = GetDirectory(directoryPath))
			return existing->Handle;

		Ref<DirectoryInfo> directoryInfo = Ref<DirectoryInfo>::Create();
		directoryInfo->Parent = parent;
		if (directoryPath == m_Project->GetAssetDirectory())
			directoryInfo->FilePath.clear();
		else
			directoryInfo->FilePath = std::filesystem::relative(directoryPath, m_Project->GetAssetDirectory()).lexically_normal();

		// An FMOD Studio project left in an older project is tooling internals (Metadata/ holds one
		// GUID-named XML per authored object), not engine content. Recursing would produce dozens of
		// folders of files the browser refuses to show, so the folder stays closed.
		const bool isStudioProjectRoot = [&directoryPath]
			{
				std::error_code ec;
				for (const auto& entry : std::filesystem::directory_iterator(directoryPath, ec))
				{
					if (ec)
						break;

					if (entry.is_regular_file(ec) && entry.path().extension() == ".fspro")
						return true;
				}
				return false;
			}();

		for (const auto& entry : std::filesystem::directory_iterator(directoryPath))
		{
			if (entry.is_directory())
			{
				if (isStudioProjectRoot)
					continue;

				// Dot-directories are tool state (.cache, .user, .git) rather than content, and
				// nothing in the browser can act on them.
				const auto directoryName = entry.path().filename().native();
				if (!directoryName.empty() && directoryName.front() == '.')
					continue;

				AssetHandle subDirectoryHandle = ProcessDirectory(entry.path(), directoryInfo);
				directoryInfo->SubDirectories[subDirectoryHandle] = m_Directories[subDirectoryHandle];
				continue;
			}

			std::filesystem::path relativePath = std::filesystem::relative(entry.path(), m_Project->GetAssetDirectory()).lexically_normal();
			AssetHandle handle = Project::GetEditorAssetManager()->GetAssetHandleFromFilePath(relativePath);
			if (!handle)
			{
				AssetType type = Project::GetEditorAssetManager()->GetAssetTypeFromPath(entry.path());
				if (type == AssetType::None)
					continue;

				handle = Project::GetEditorAssetManager()->ImportAsset(entry.path());
			}

			if (handle)
				directoryInfo->Assets.push_back(handle);
		}

		m_Directories[directoryInfo->Handle] = directoryInfo;
		return directoryInfo->Handle;
	}

	void ContentBrowserPanel::RebuildItems()
	{
		m_CurrentItems.Items.clear();

		if (strlen(m_SearchBuffer) == 0)
		{
			for (const auto& [subdirHandle, subdir] : m_CurrentDirectory->SubDirectories)
				m_CurrentItems.Items.push_back(Ref<ContentBrowserDirectory>::Create(subdir));

			for (AssetHandle assetHandle : m_CurrentDirectory->Assets)
			{
				AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(assetHandle);
				if (!metadata.IsValid())
					continue;

				m_CurrentItems.Items.push_back(Ref<ContentBrowserAsset>::Create(metadata, GetAssetIcon(metadata)));
			}
		}
		else
		{
			m_CurrentItems = Search(m_SearchBuffer, m_CurrentDirectory);
		}

		SortItemList();
		ClearSelections();
	}

	void ContentBrowserPanel::ChangeDirectory(const Ref<DirectoryInfo>& directory)
	{
		if (!directory)
			return;
		m_PreviousDirectory = m_CurrentDirectory;
		m_CurrentDirectory = directory;
		m_NextDirectory.reset();
		m_BreadCrumbData.clear();
		for (auto current = m_CurrentDirectory; current && current->Parent; current = current->Parent)
			m_BreadCrumbData.push_back(current);
		std::reverse(m_BreadCrumbData.begin(), m_BreadCrumbData.end());
		m_LocationLabel = Utils::PathToUTF8(std::filesystem::path("Assets") / m_CurrentDirectory->FilePath);
		RebuildItems();
	}

	void ContentBrowserPanel::OnBrowseBack()
	{
		if (!m_CurrentDirectory || !m_CurrentDirectory->Parent)
			return;

		const auto previous = m_CurrentDirectory;
		ChangeDirectory(m_CurrentDirectory->Parent);
		m_NextDirectory = previous;
	}

	void ContentBrowserPanel::OnBrowseForward()
	{
		if (m_NextDirectory)
			ChangeDirectory(m_NextDirectory);
	}

	void ContentBrowserPanel::RenderFavorites()
	{
		if (m_Favorites.empty())
			return;

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker));
		ImGui::TextUnformatted("FAVORITES");
		ImGui::PopStyleColor();

		// A removal (ToggleFavorite) mutates m_Favorites, so it can't run inside the range-for over it —
		// that invalidates the iterator and dangles `fav`. Defer it to after the loop.
		std::string favoriteToRemove;
		for (const std::string& fav : m_Favorites)
		{
			const std::filesystem::path favPath(std::u8string(fav.begin(), fav.end()));
			Ref<DirectoryInfo> dir = GetDirectory(favPath);

			ImGui::PushID(fav.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent));
			const std::string label =
				std::format("{}  {}", LUX_ICON_STAR, Utils::PathToUTF8(favPath.filename()));
			const bool clicked = ImGui::Selectable(label.c_str(), false);
			ImGui::PopStyleColor();

			if (ImGui::BeginPopupContextItem("##FavCtx"))
			{
				if (ImGui::MenuItem("Remove from Favorites"))
					favoriteToRemove = fav;
				ImGui::EndPopup();
			}
			if (clicked && dir)
				ChangeDirectory(dir);
			ImGui::PopID();
		}

		if (!favoriteToRemove.empty())
			ToggleFavorite(favoriteToRemove);

		ImGui::Separator();
		ImGui::Spacing();
	}

	void ContentBrowserPanel::RenderDirectoryHierarchy(const Ref<DirectoryInfo>& directory)
	{
		const bool isSelected = m_CurrentDirectory && m_CurrentDirectory->Handle == directory->Handle;
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (isSelected)
			flags |= ImGuiTreeNodeFlags_Selected;
		if (directory->SubDirectories.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;

		std::string label = Utils::PathToUTF8(directory->FilePath.filename());
		if (label.empty())
			label = m_Project->GetConfig().AssetDirectory.string();

		const bool opened = ImGui::TreeNodeEx(reinterpret_cast<const void*>(static_cast<uint64_t>(directory->Handle)), flags, "%s", label.c_str());
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			ChangeDirectory(directory);

		UpdateDropArea(directory);

		// Unique per directory — a shared string id would make every node's context menu collide, so a
		// right-click could open the wrong directory's menu.
		const std::string contextId = std::format("##DirectoryContext_{}", static_cast<uint64_t>(directory->Handle));
		if (ImGui::BeginPopupContextItem(contextId.c_str()))
		{
			const std::string genericPath = Utils::PathToUTF8(directory->FilePath);
			const bool fav = IsFavorite(genericPath);
			if (ImGui::MenuItem(fav ? LUX_ICON_STAR "  Remove from Favorites" : LUX_ICON_STAR_O "  Add to Favorites"))
				ToggleFavorite(genericPath);
			ImGui::EndPopup();
		}

		if (opened)
		{
			std::vector<Ref<DirectoryInfo>> directories;
			directories.reserve(directory->SubDirectories.size());
			for (const auto& [handle, subdir] : directory->SubDirectories)
				directories.emplace_back(subdir);

			std::sort(directories.begin(), directories.end(),
					  [](const Ref<DirectoryInfo>& a, const Ref<DirectoryInfo>& b)
					  {
						  return Utils::String::ToLowerCopy(Utils::PathToUTF8(a->FilePath.filename())) <
								 Utils::String::ToLowerCopy(Utils::PathToUTF8(b->FilePath.filename()));
					  });

			for (const auto& subdir : directories)
				RenderDirectoryHierarchy(subdir);

			ImGui::TreePop();
		}
	}

	void ContentBrowserPanel::RenderFolders()
	{
		if (ImGui::Selectable("Assets", m_CurrentDirectory == m_BaseDirectory))
			ChangeDirectory(m_BaseDirectory);
		RenderFavorites();
		for (const auto& directory : m_FolderRoots)
			RenderDirectoryHierarchy(directory);
	}

	void ContentBrowserPanel::RenderTopBar()
	{
		enum Action
		{
			New,
			Back,
			Forward,
			Folders,
			Filter,
			View,
			Sort,
			Options,
			RefreshItems,
			ClearThumbnails
		};
		const bool wide = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 25.0f;
		const ImGuiEx::PanelAction actions[] = {
			{New, "New", LUX_ICON_PLUS, nullptr, true, false, true},
			{Back, "Back", LUX_ICON_ARROW_LEFT, "Alt+Left", m_CurrentDirectory && m_CurrentDirectory->Parent},
			{Forward, "Forward", LUX_ICON_ARROW_RIGHT, "Alt+Right", m_NextDirectory != nullptr},
			{Folders, wide ? "Folders" : "Browse folders", LUX_ICON_FOLDER, nullptr, true,
			 wide && m_ShowFolders},
			{Filter, "Filter", LUX_ICON_FILTER, nullptr, true, m_TypeFilter != 0},
			{View, IsListView() ? "Grid view" : "List view",
			 IsListView() ? LUX_ICON_TH_LARGE : LUX_ICON_LIST},
			{Sort, "Sort", LUX_ICON_SORT},
			{Options, "View options", LUX_ICON_COG},
			{RefreshItems, "Refresh", LUX_ICON_REFRESH, "F5", true, false, false, true},
			{ClearThumbnails, "Clear thumbnail cache", nullptr, nullptr, true, false, false, true}};
		switch (ImGuiEx::PanelActionBar("##browser_actions", actions))
		{
		case New:
			ImGui::OpenPopup("ContentBrowserNew");
			break;
		case Back:
			OnBrowseBack();
			break;
		case Forward:
			OnBrowseForward();
			break;
		case Folders:
			if (wide)
				m_ShowFolders = !m_ShowFolders;
			else
				ImGui::OpenPopup("ContentBrowserFolders");
			break;
		case Filter:
			ImGui::OpenPopup("ContentBrowserFilter");
			break;
		case View:
			m_ViewMode = IsListView() ? ViewMode::Grid : ViewMode::List;
			SaveSettings();
			break;
		case Sort:
			ImGui::OpenPopup("ContentBrowserSort");
			break;
		case Options:
			ImGui::OpenPopup("ContentBrowserSettings");
			break;
		case RefreshItems:
			Refresh();
			break;
		case ClearThumbnails:
			if (m_ThumbnailCache)
				m_ThumbnailCache->Clear();
			if (m_MaterialThumbnailer)
				m_MaterialThumbnailer->Clear();
			if (m_WaveformThumbnailer)
				m_WaveformThumbnailer->Clear();
			break;
		default:
			break;
		}

		const float width = ImGui::GetContentRegionAvail().x;
		const bool singleRow = width >= ImGui::GetFontSize() * 28.0f;
		const float pathWidth = singleRow ? width * 0.4f : width;
		if (ImGui::Button("##browser_location", {pathWidth, ImGui::GetFrameHeight()}))
			ImGui::OpenPopup("ContentBrowserLocation");
		ImGui::SetItemTooltip("%s\nChoose a parent folder", m_LocationLabel.c_str());
		const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
		ImGui::RenderTextEllipsis(
			ImGui::GetWindowDrawList(),
			{minimum.x + ImGui::GetStyle().FramePadding.x, minimum.y + ImGui::GetStyle().FramePadding.y},
			{maximum.x - ImGui::GetStyle().FramePadding.x, maximum.y},
			maximum.x - ImGui::GetStyle().FramePadding.x, m_LocationLabel.c_str(), nullptr, nullptr);
		UpdateDropArea(m_CurrentDirectory);
		if (singleRow)
			ImGui::SameLine();
		if (m_FocusSearchWidget)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusSearchWidget = false;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##browser_search", "Search assets... (Ctrl+F)", m_SearchBuffer,
									 sizeof(m_SearchBuffer)))
			RebuildItems();

		if (ImGui::BeginPopup("ContentBrowserLocation"))
		{
			Ref<DirectoryInfo> destination;
			if (ImGui::MenuItem("Assets", nullptr, m_CurrentDirectory == m_BaseDirectory))
				destination = m_BaseDirectory;
			// Changing directory does not mutate the cached ancestor vector until the next navigation.
			for (const auto& directory : m_BreadCrumbData)
			{
				ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(directory->Handle)));
				const std::string label = Utils::PathToUTF8(directory->FilePath);
				if (ImGui::MenuItem(label.c_str(), nullptr, directory == m_CurrentDirectory))
					destination = directory;
			}
			if (destination)
				ChangeDirectory(destination);
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("ContentBrowserFolders"))
		{
			ImGui::BeginChild("##folder_choices", {std::min(ImGui::GetFontSize() * 24.0f,
															ImGui::GetMainViewport()->WorkSize.x * 0.8f),
												   ImGui::GetFontSize() * 15.0f});
			RenderFolders();
			ImGui::EndChild();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("ContentBrowserFilter"))
		{
			for (int i = 0; i < IM_ARRAYSIZE(s_FilterLabels); ++i)
				if (ImGui::MenuItem(s_FilterLabels[i], nullptr, m_TypeFilter == i))
				{
					m_TypeFilter = i;
					ClearSelections();
					SaveSettings();
				}
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("ContentBrowserNew"))
		{
			if (ImGui::MenuItem("Folder"))
				CreateNewFolder();
			if (ImGui::MenuItem("C# Script..."))
				BeginNewScript();
			if (ImGui::MenuItem("Scene"))
				CreateSceneAsset();
			if (ImGui::MenuItem("Material"))
				CreateMaterialAsset();
			if (ImGui::MenuItem("Audio Surface Table"))
				CreateAudioSurfaceTable();
			if (ImGui::MenuItem("Dialogue Table"))
				CreateDialogueTable();
			if (ImGui::MenuItem("Sound"))
				CreateSoundAsset();
			if (ImGui::MenuItem("Sound Bank"))
				CreateSoundBankAsset();
			ImGui::Separator();
			if (ImGui::MenuItem("Import…"))
			{
				std::filesystem::path filepath = FileSystem::OpenFileDialog();
				if (!filepath.empty())
				{
					const std::filesystem::path destinationDirectory = Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath;
					if (FileSystem::CopyFile(filepath, destinationDirectory))
					{
						CopyImportedMeshDependencies(filepath, destinationDirectory);
						Refresh();
					}
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("ContentBrowserSort"))
		{
			bool changed = false;
			if (ImGui::MenuItem("Name", nullptr, m_SortMode == SortMode::Name)) { m_SortMode = SortMode::Name; changed = true; }
			if (ImGui::MenuItem("Type", nullptr, m_SortMode == SortMode::Type)) { m_SortMode = SortMode::Type; changed = true; }
			if (ImGui::MenuItem("Recently modified", nullptr, m_SortMode == SortMode::Modified)) { m_SortMode = SortMode::Modified; changed = true; }
			ImGui::Separator();
			if (ImGui::MenuItem("Ascending", nullptr, m_SortAscending)) { m_SortAscending = true; changed = true; }
			if (ImGui::MenuItem("Descending", nullptr, !m_SortAscending)) { m_SortAscending = false; changed = true; }
			if (changed)
			{
				SortItemList();
				SaveSettings();
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("ContentBrowserSettings"))
		{
			bool saveSettings = false;
			if (ImGui::Checkbox("Show Asset Types", &m_ShowAssetType))
				saveSettings = true;

			if (ImGui::SliderFloat("Thumbnail Size", &m_ThumbnailSize, 32.0f, 256.0f, "%.0f"))
				saveSettings = true;

			if (saveSettings)
				SaveSettings();

			ImGui::EndPopup();
		}
	}

	void ContentBrowserPanel::RenderItems()
	{
		m_IsAnyItemHovered = false;
		m_VisibleItems = 0;

		for (auto& item : m_CurrentItems.Items)
		{
			// Type-filter chips: hide assets that don't match the active filter (folders always show).
			if (m_TypeFilter != 0 && item->GetType() == ContentBrowserItem::ItemType::Asset && !AssetMatchesFilter(item->GetID()))
				continue;

			++m_VisibleItems;
			item->OnRenderBegin();
			CBItemActionResult result = item->OnRender(this);

			if (result.IsSet(ContentBrowserAction::ClearSelections))
				ClearSelections();

			if (result.IsSet(ContentBrowserAction::Deselected))
				DeselectItem(item->GetID());

			if (result.IsSet(ContentBrowserAction::Selected))
				SelectItem(item->GetID());

			if (result.IsSet(ContentBrowserAction::SelectToHere))
				SelectRangeTo(item->GetID());

			if (result.IsSet(ContentBrowserAction::StartRenaming))
				item->StartRenaming();

			if (result.IsSet(ContentBrowserAction::Copy))
				m_CopiedAssets.CopyFrom(m_SelectedItems);

			if (result.IsSet(ContentBrowserAction::Reload) && item->GetType() == ContentBrowserItem::ItemType::Asset)
				AssetManager::ReloadData(item->GetID());

			if (result.IsSet(ContentBrowserAction::OpenDeleteDialogue) && !item->IsRenaming())
				m_OpenDeletePopup = true;

			if (result.IsSet(ContentBrowserAction::ShowInExplorer))
			{
				if (item->GetType() == ContentBrowserItem::ItemType::Directory)
				{
					if (auto directoryItem = item.As<ContentBrowserDirectory>())
						FileSystem::ShowFileInExplorer(Project::GetActiveAssetDirectory() / directoryItem->GetDirectoryInfo()->FilePath);
				}
				else
				{
					FileSystem::ShowFileInExplorer(Project::GetEditorAssetManager()->GetFileSystemPath(item->GetID()));
				}
			}

			if (result.IsSet(ContentBrowserAction::OpenExternal))
			{
				if (item->GetType() == ContentBrowserItem::ItemType::Directory)
				{
					if (auto directoryItem = item.As<ContentBrowserDirectory>())
						FileSystem::OpenExternally(Project::GetActiveAssetDirectory() / directoryItem->GetDirectoryInfo()->FilePath);
				}
				else
				{
					FileSystem::OpenExternally(Project::GetEditorAssetManager()->GetFileSystemPath(item->GetID()));
				}
			}

			if (result.IsSet(ContentBrowserAction::Hovered))
				m_IsAnyItemHovered = true;

			item->OnRenderEnd();

			if (result.IsSet(ContentBrowserAction::Duplicate))
			{
				m_CopiedAssets.CopyFrom(m_SelectedItems);
				PasteCopiedAssets();
				break;
			}

			if (result.IsSet(ContentBrowserAction::Renamed))
			{
				Refresh();
				break;
			}

			if (result.IsSet(ContentBrowserAction::Activated))
			{
				if (item->GetType() == ContentBrowserItem::ItemType::Directory)
				{
					if (auto directoryItem = item.As<ContentBrowserDirectory>())
						ChangeDirectory(directoryItem->GetDirectoryInfo());
				}
				else if (auto assetItem = item.As<ContentBrowserAsset>())
				{
					ActivateAsset(assetItem->GetAssetInfo());
				}

				break;
			}

			if (result.IsSet(ContentBrowserAction::Refresh))
			{
				Refresh();
				break;
			}

			// CreatePrefabVariant refreshes the item list, so exit the loop right after (same as above).
			if (result.IsSet(ContentBrowserAction::CreateVariant) && item->GetType() == ContentBrowserItem::ItemType::Asset)
			{
				if (AssetManager::GetAssetType(item->GetID()) == AssetType::Material)
					CreateMaterialInstance(item->GetID());
				else
					CreatePrefabVariant(item->GetID());
				break;
			}

			// Creating the Sound refreshes the item list too.
			if (result.IsSet(ContentBrowserAction::CreateSound) && item->GetType() == ContentBrowserItem::ItemType::Asset)
			{
				CreateSoundAsset(GetSoundClips(item->GetID()));
				break;
			}
		}

		if (!m_VisibleItems)
			ImGuiEx::PanelEmptyState(
				"##browser_empty",
				m_SearchBuffer[0] || m_TypeFilter ? "No matching assets" : "This folder is empty",
				m_SearchBuffer[0] || m_TypeFilter ? "Change the search or choose All in the filter menu."
												  : "Use New to create an asset or import a file.");
		if (m_OpenDeletePopup)
		{
			ImGui::OpenPopup("Delete");
			m_OpenDeletePopup = false;
		}
	}

	void ContentBrowserPanel::RenderBottomBar(float height)
	{
		ImGui::BeginChild("##content_browser_bottombar", {0.0f, height}, 0,
						  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const size_t count = GetSelectionCount();
		const AssetHandle selected = count == 1 ? GetSelection(0) : AssetHandle(0);
		if (selected != m_StatusSelection || count != m_StatusCount || m_StatusText.empty())
		{
			m_StatusSelection = selected;
			m_StatusCount = count;
			if (count == 1 && m_Directories.contains(selected))
				m_StatusText =
					Utils::PathToUTF8(std::filesystem::path("Assets") / m_Directories.at(selected)->FilePath);
			else if (count == 1)
			{
				const auto metadata = Project::GetEditorAssetManager()->GetMetadata(selected);
				std::error_code error;
				const auto bytes =
					std::filesystem::file_size(Project::GetActiveAssetDirectory() / metadata.FilePath, error);
				m_StatusText = std::format("{} | {} | {}", Utils::PathToUTF8(metadata.FilePath),
										   AssetTypeToString(metadata.Type),
										   error ? "Size unavailable" : Utils::BytesToString(bytes));
			}
			else if (count > 1)
				m_StatusText = std::format("{} items selected", count);
			else
				m_StatusText.clear();
		}
		char itemCount[64];
		std::snprintf(itemCount, sizeof(itemCount), "%zu visible items | %s", m_VisibleItems,
					  s_FilterLabels[m_TypeFilter]);
		const char* text = count ? m_StatusText.c_str() : itemCount;
		const float available = ImGui::GetContentRegionAvail().x;
		const bool slider = !IsListView() && available >= ImGui::GetFontSize() * 25.0f;
		const float sliderWidth = slider ? ImGui::GetFontSize() * 8.0f : 0.0f;
		const float textWidth = std::max(1.0f, available - sliderWidth - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InvisibleButton("##selection_status", {textWidth, ImGui::GetFrameHeight()});
		ImGui::SetItemTooltip("%s", text);
		ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
		minimum.y += ImGui::GetStyle().FramePadding.y;
		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum, maximum, maximum.x, text, nullptr,
									  nullptr);
		}
		if (slider)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(sliderWidth);
			if (ImGui::SliderFloat("##thumbsize", &m_ThumbnailSize, 32.0f, 256.0f, ""))
				SaveSettings();
			ImGui::SetItemTooltip("Thumbnail size (also in View options)");
		}
		ImGui::EndChild();
	}

	float ContentBrowserPanel::GetThumbnailSize() const
	{
		return m_ThumbnailSize * ImGui::GetStyle()._MainScale;
	}

	void ContentBrowserPanel::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "ShowFolders" << Yaml::Value << m_ShowFolders;
		out << Yaml::Key << "FolderFraction" << Yaml::Value << m_FolderFraction;
	}

	void ContentBrowserPanel::LoadState(const Yaml::Node& node)
	{
		m_ShowFolders = node["ShowFolders"].as<bool>(true);
		const float fraction = node["FolderFraction"].as<float>(0.2f);
		m_FolderFraction = std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 0.2f;
	}

	void ContentBrowserPanel::CreatePrefabVariant(AssetHandle baseHandle)
	{
		Ref<Prefab> base = AssetManager::GetAsset<Prefab>(baseHandle);
		if (!base || !base->GetScene())
			return;

		auto assetManager = Project::GetEditorAssetManager();
		const std::string extension = assetManager->GetDefaultExtensionForAssetType(AssetType::Prefab);
		const std::filesystem::path baseAbsolute = assetManager->GetFileSystemPath(baseHandle);
		const std::filesystem::path targetPath = FileSystem::GetUniqueFileName(
			baseAbsolute.parent_path() / (baseAbsolute.stem().string() + " Variant" + extension));

		// A variant is a self-contained copy of the base's scene that remembers its base.
		Ref<Scene> variantScene = Scene::Copy(base->GetScene());
		PrefabSerializer::WritePrefabFile(targetPath, variantScene, baseHandle);

		const std::filesystem::path relativePath = std::filesystem::relative(targetPath, Project::GetActiveAssetDirectory());
		const AssetHandle handle = assetManager->ImportAsset(relativePath);
		if (handle)
			LUX_CONSOLE_LOG_INFO("Created prefab variant '{}'", relativePath.generic_string());
		else
			LUX_CONSOLE_LOG_ERROR("Failed to register prefab variant '{}'", relativePath.generic_string());

		Refresh();
	}

	void ContentBrowserPanel::Refresh()
	{
		if (!m_Project)
			return;

		std::filesystem::path currentPath;
		if (m_CurrentDirectory)
			currentPath = m_CurrentDirectory->FilePath;

		m_CurrentItems.Clear();
		m_StatusText.clear();
		m_Directories.clear();

		AssetHandle baseDirectoryHandle = ProcessDirectory(m_Project->GetAssetDirectory(), nullptr);
		m_BaseDirectory = m_Directories[baseDirectoryHandle];
		m_FolderRoots.clear();
		for (const auto& [handle, directory] : m_BaseDirectory->SubDirectories)
			m_FolderRoots.push_back(directory);
		std::ranges::sort(m_FolderRoots, {}, [](const Ref<DirectoryInfo>& directory)
						  { return Utils::String::ToLowerCopy(Utils::PathToUTF8(directory->FilePath)); });
		m_CurrentDirectory = GetDirectory(currentPath);
		if (!m_CurrentDirectory)
			m_CurrentDirectory = m_BaseDirectory;

		ChangeDirectory(m_CurrentDirectory);
	}

	void ContentBrowserPanel::UpdateInput()
	{
		if (!m_IsContentBrowserHovered)
			return;

		if ((!m_IsAnyItemHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			|| Input::IsKeyDown(KeyCode::Escape))
		{
			ClearSelections();
		}

		if (Input::IsKeyDown(KeyCode::F5))
			Refresh();
	}

	bool ContentBrowserPanel::OnKeyPressedEvent(KeyPressedEvent& e)
	{
		if (!m_IsContentBrowserFocused)
			return false;
		if (ImGui::GetIO().WantTextInput || ImGui::GetTopMostPopupModal())
			return true;
		if (e.GetRepeatCount() != 0)
			return true;

		bool handled = false;
		const bool control = Input::IsKeyDown(KeyCode::LeftControl) || Input::IsKeyDown(KeyCode::RightControl);
		const bool shift = Input::IsKeyDown(KeyCode::LeftShift) || Input::IsKeyDown(KeyCode::RightShift);
		const bool alt = Input::IsKeyDown(KeyCode::LeftAlt) || Input::IsKeyDown(KeyCode::RightAlt);

		if (control)
		{
			switch (e.GetKeyCode())
			{
			case KeyCode::C:
				m_CopiedAssets.CopyFrom(m_SelectedItems);
				handled = true;
				break;
			case KeyCode::V:
				PasteCopiedAssets();
				handled = true;
				break;
			case KeyCode::D:
				m_CopiedAssets.CopyFrom(m_SelectedItems);
				PasteCopiedAssets();
				handled = true;
				break;
			case KeyCode::F:
				m_FocusSearchWidget = true;
				handled = true;
				break;
			default:
				break;
			}

			if (shift && e.GetKeyCode() == KeyCode::N)
			{
				CreateNewFolder();
				handled = true;
			}
		}

		if (alt)
		{
			if (e.GetKeyCode() == KeyCode::Left)
			{
				OnBrowseBack();
				handled = true;
			}
			else if (e.GetKeyCode() == KeyCode::Right)
			{
				OnBrowseForward();
				handled = true;
			}
		}

		if (e.GetKeyCode() == KeyCode::Delete && GetSelectionCount() > 0)
		{
			for (const auto& item : m_CurrentItems)
			{
				if (item->IsRenaming())
					return false;
			}

			m_OpenDeletePopup = true;
			handled = true;
		}

		return handled;
	}

	bool ContentBrowserPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		if (!m_IsContentBrowserFocused)
			return false;

		if (e.GetMouseButton() == MouseButton::Button3)
		{
			OnBrowseBack();
			return true;
		}

		if (e.GetMouseButton() == MouseButton::Button4)
		{
			OnBrowseForward();
			return true;
		}

		return false;
	}

	void ContentBrowserPanel::PasteCopiedAssets()
	{
		if (m_CopiedAssets.SelectionCount() == 0 || !m_CurrentDirectory)
			return;

		bool copiedAny = false;
		const std::filesystem::path currentDirectoryPath = Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath;

		for (AssetHandle handle : m_CopiedAssets)
		{
			if (m_Directories.contains(handle))
			{
				const Ref<DirectoryInfo>& directory = m_Directories.at(handle);
				const std::filesystem::path sourcePath = Project::GetActiveAssetDirectory() / directory->FilePath;
				const std::filesystem::path destinationPath = MakeUniqueDuplicatePath(currentDirectoryPath / sourcePath.filename());

				std::error_code ec;
				std::filesystem::copy(sourcePath, destinationPath, std::filesystem::copy_options::recursive, ec);
				copiedAny |= !ec;
				continue;
			}

			AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
			if (!metadata.IsValid())
				continue;

			const std::filesystem::path sourcePath = Project::GetEditorAssetManager()->GetFileSystemPath(metadata);
			const std::filesystem::path destinationPath = MakeUniqueDuplicatePath(currentDirectoryPath / sourcePath.filename());
			if (!FileSystem::Copy(sourcePath, destinationPath))
				continue;

			Project::GetEditorAssetManager()->ImportAsset(destinationPath);
			copiedAny = true;
		}

		if (copiedAny)
			Refresh();
	}

	void ContentBrowserPanel::RenderDeleteDialogue()
	{
		if (!ImGui::BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (GetSelectionCount() == 0)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::Text("Delete %llu selected item(s)?", static_cast<unsigned long long>(GetSelectionCount()));

		if (ImGui::Button("Yes", ImVec2(60.0f, 0.0f)))
		{
			std::vector<AssetHandle> selections(m_SelectedItems.begin(), m_SelectedItems.end());
			for (AssetHandle handle : selections)
			{
				if (m_Directories.contains(handle))
					DeleteDirectory(m_Directories.at(handle));
				else
					DeleteAsset(handle);
			}

			ClearSelections();
			Refresh();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("No", ImVec2(60.0f, 0.0f)))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	void ContentBrowserPanel::RemoveDirectory(Ref<DirectoryInfo> directory, bool removeFromParent)
	{
		if (!directory)
			return;

		if (directory->Parent && removeFromParent)
			directory->Parent->SubDirectories.erase(directory->Handle);

		for (const auto& [handle, subdir] : directory->SubDirectories)
			RemoveDirectory(subdir, false);

		m_Directories.erase(directory->Handle);
	}

	void ContentBrowserPanel::UpdateDropArea(const Ref<DirectoryInfo>& target)
	{
		if (!target || (m_CurrentDirectory && target->Handle == m_CurrentDirectory->Handle))
			return;

		if (!ImGui::BeginDragDropTarget())
			return;

		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM");
		if (payload)
		{
			if (MoveSelectionTo(target->FilePath))
				Refresh();
		}

		ImGui::EndDragDropTarget();
	}

	bool ContentBrowserPanel::AssetMatchesFilter(AssetHandle handle) const
	{
		if (m_TypeFilter == 0)
			return true;
		const AssetType t = AssetManager::GetAssetType(handle);
		switch (m_TypeFilter)
		{
			case 1: return t == AssetType::Mesh || t == AssetType::StaticMesh || t == AssetType::MeshSource;
			case 2: return t == AssetType::Material;
			case 3: return t == AssetType::Texture || t == AssetType::EnvMap;
			case 4: return t == AssetType::Script || t == AssetType::ScriptFile;
			case 5: return t == AssetType::Scene;
			case 6: return t == AssetType::Prefab;
			default: return true;
		}
	}

	bool ContentBrowserPanel::IsFavorite(const std::string& genericPath) const
	{
		return std::find(m_Favorites.begin(), m_Favorites.end(), genericPath) != m_Favorites.end();
	}

	void ContentBrowserPanel::ToggleFavorite(const std::string& genericPath)
	{
		auto it = std::find(m_Favorites.begin(), m_Favorites.end(), genericPath);
		if (it != m_Favorites.end())
			m_Favorites.erase(it);
		else
			m_Favorites.push_back(genericPath);
		SaveSettings();
	}

	void ContentBrowserPanel::SortItemList()
	{
		const std::filesystem::path assetDir = Project::GetActiveAssetDirectory();

		auto modTime = [&](const Ref<ContentBrowserItem>& item) -> long long
		{
			std::filesystem::path rel;
			if (item->GetType() == ContentBrowserItem::ItemType::Directory)
			{
				auto dirIt = m_Directories.find(item->GetID());
				if (dirIt != m_Directories.end())
					rel = dirIt->second->FilePath;
			}
			else
			{
				rel = Project::GetEditorAssetManager()->GetMetadata(item->GetID()).FilePath;
			}
			std::error_code ec;
			const auto t = std::filesystem::last_write_time(assetDir / rel, ec);
			return ec ? 0 : (long long)t.time_since_epoch().count();
		};

		// A strict-weak-ordering "less than" for the active sort mode. Descending reverses the operands
		// rather than negating the result — negating would make equal elements compare less in both
		// directions, which is undefined behaviour for std::sort.
		auto lessThan = [&](const Ref<ContentBrowserItem>& a, const Ref<ContentBrowserItem>& b)
		{
			switch (m_SortMode)
			{
				case SortMode::Type:
				{
					const std::string ta = a->GetType() == ContentBrowserItem::ItemType::Asset ? std::string(AssetTypeToString(AssetManager::GetAssetType(a->GetID()))) : std::string();
					const std::string tb = b->GetType() == ContentBrowserItem::ItemType::Asset ? std::string(AssetTypeToString(AssetManager::GetAssetType(b->GetID()))) : std::string();
					return (ta != tb) ? ta < tb : Utils::String::ToLowerCopy(a->GetName()) < Utils::String::ToLowerCopy(b->GetName());
				}
				case SortMode::Modified:
				{
					const long long ma = modTime(a), mb = modTime(b);
					return (ma != mb) ? ma < mb : Utils::String::ToLowerCopy(a->GetName()) < Utils::String::ToLowerCopy(b->GetName());
				}
				default:
					return Utils::String::ToLowerCopy(a->GetName()) < Utils::String::ToLowerCopy(b->GetName());
			}
		};

		std::sort(m_CurrentItems.begin(), m_CurrentItems.end(), [&](const Ref<ContentBrowserItem>& a, const Ref<ContentBrowserItem>& b)
		{
			// Directories always precede assets, regardless of sort direction.
			if (a->GetType() != b->GetType())
				return static_cast<uint16_t>(a->GetType()) < static_cast<uint16_t>(b->GetType());

			return m_SortAscending ? lessThan(a, b) : lessThan(b, a);
		});
	}

	void ContentBrowserPanel::LoadSettings()
	{
		auto& settings = Application::Get().GetSettings();
		m_ThumbnailSize = settings.GetFloat("ContentBrowser.ThumbnailSize", 128.0f);
		m_ShowAssetType = settings.GetInt("ContentBrowser.ShowAssetTypes", 1) != 0;
		m_ViewMode = static_cast<ViewMode>(std::clamp(settings.GetInt("ContentBrowser.ViewMode", 0), 0, 1));
		m_SortMode = static_cast<SortMode>(std::clamp(settings.GetInt("ContentBrowser.SortMode", 0), 0, 2));
		m_SortAscending = settings.GetInt("ContentBrowser.SortAscending", 1) != 0;
		m_TypeFilter = std::clamp(settings.GetInt("ContentBrowser.TypeFilter", 0), 0, (int)IM_ARRAYSIZE(s_FilterLabels) - 1);

		m_Favorites.clear();
		const std::string favs = settings.Get("ContentBrowser.Favorites", "");
		size_t start = 0;
		while (start <= favs.size())
		{
			const size_t bar = favs.find('|', start);
			const std::string one = favs.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
			if (!one.empty())
				m_Favorites.push_back(one);
			if (bar == std::string::npos)
				break;
			start = bar + 1;
		}
	}

	void ContentBrowserPanel::SaveSettings() const
	{
		auto& settings = Application::Get().GetSettings();
		settings.SetFloat("ContentBrowser.ThumbnailSize", m_ThumbnailSize);
		settings.SetInt("ContentBrowser.ShowAssetTypes", m_ShowAssetType ? 1 : 0);
		settings.SetInt("ContentBrowser.ViewMode", static_cast<int>(m_ViewMode));
		settings.SetInt("ContentBrowser.SortMode", static_cast<int>(m_SortMode));
		settings.SetInt("ContentBrowser.SortAscending", m_SortAscending ? 1 : 0);
		settings.SetInt("ContentBrowser.TypeFilter", m_TypeFilter);

		std::string favs;
		for (size_t i = 0; i < m_Favorites.size(); i++)
		{
			if (i)
				favs += '|';
			favs += m_Favorites[i];
		}
		settings.Set("ContentBrowser.Favorites", favs);
		settings.Serialize();
	}

	void ContentBrowserPanel::SetThumbnailSize(float size)
	{
		m_ThumbnailSize = std::clamp(size, 32.0f, 256.0f);
		SaveSettings();
	}

	void ContentBrowserPanel::SetShowAssetTypes(bool show)
	{
		m_ShowAssetType = show;
		SaveSettings();
	}

	void ContentBrowserPanel::StartRenamingItem(AssetHandle handle)
	{
		size_t index = m_CurrentItems.FindItem(handle);
		if (index != ContentBrowserItemList::InvalidItem)
			m_CurrentItems[index]->StartRenaming();
	}

	void ContentBrowserPanel::ActivateAsset(const AssetMetadata& metadata)
	{
		auto callback = m_ItemActivationCallbacks.find(metadata.Type);
		if (callback != m_ItemActivationCallbacks.end())
		{
			callback->second(metadata);
			return;
		}

		FileSystem::OpenExternally(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
	}

	void ContentBrowserPanel::BeginNewScript()
	{
		if (!m_Project)
			return;
		const auto sourceDirectory = m_Project->GetAssetDirectory() / "Scripts" / "Source";
		const std::string name = Utils::AddSuffixToMakeUnique("NewScript", [&sourceDirectory](const std::string& candidate)
		{
			return FileSystem::Exists(sourceDirectory / (candidate + ".cs"));
		});
		std::snprintf(m_NewScriptName, sizeof(m_NewScriptName), "%s", name.c_str());
		m_ScriptCreateError.clear();
		m_ScriptPreview.clear();
		if (ScriptTemplate::Load(FileSystem::GetWorkingDirectory() / "Resources" / "Templates" / "NewClassTemplate.cs", m_ScriptTemplate, m_ScriptTemplateError))
			UpdateScriptPreview();
		else
			LUX_CORE_ERROR_TAG("ScriptEngine", "{}", m_ScriptTemplateError);
		m_OpenNewScriptPopup = true;
		m_NewScriptDialogOpen = true;
		m_FocusNewScriptName = true;
	}

	void ContentBrowserPanel::UpdateScriptPreview()
	{
		m_ScriptCreateError.clear();
		if (!m_ScriptTemplate.empty() && m_Project)
			ScriptTemplate::Render(m_ScriptTemplate, m_NewScriptName, m_Project->GetConfig().DefaultNamespace, m_ScriptPreview, m_ScriptTemplateError);
	}

	void ContentBrowserPanel::RenderNewScriptDialogue()
	{
		if (m_OpenNewScriptPopup)
		{
			ImGui::OpenPopup("New C# Script");
			m_OpenNewScriptPopup = false;
		}
		ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_Appearing);
		if (!ImGui::BeginPopupModal("New C# Script", &m_NewScriptDialogOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (!m_Project || !m_NewScriptDialogOpen)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::TextUnformatted("Class name");
		if (m_FocusNewScriptName)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusNewScriptName = false;
		}
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##new_script_class", m_NewScriptName, sizeof(m_NewScriptName), ImGuiInputTextFlags_AutoSelectAll))
			UpdateScriptPreview();
		ImGui::TextWrapped("Namespace: %s", m_Project->GetConfig().DefaultNamespace.c_str());
		ImGui::TextWrapped("File: Assets/Scripts/Source/%s.cs", m_NewScriptName);
		ImGui::Separator();
		ImGui::BeginChild("##script_preview", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(m_ScriptPreview.c_str());
		ImGui::EndChild();

		const auto path = m_Project->GetAssetDirectory() / "Scripts" / "Source" / (std::string(m_NewScriptName) + ".cs");
		std::string validationError = m_ScriptTemplateError;
		if (validationError.empty() && FileSystem::Exists(path))
			validationError = "A script with that name already exists. Choose a different class name.";
		const std::string& error = validationError.empty() ? m_ScriptCreateError : validationError;
		if (!error.empty())
			ImGui::TextWrapped("%s", error.c_str());
		ImGui::TextWrapped("Build Scripts, reload the C# assembly, then choose this class in an entity's Script component.");

		bool create = false;
		{
			ImGuiEx::ScopedDisable disabled(!validationError.empty());
			create = ImGui::Button("Create Script");
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_NewScriptDialogOpen = false;
			ImGui::CloseCurrentPopup();
			create = false;
		}
		if (create)
		{
			m_ScriptCreateError.clear();
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			if (ec)
				m_ScriptCreateError = "Could not create the script folder: " + ec.message();
			else if (!FileSystem::WriteFileAtomically(path, m_ScriptPreview))
				m_ScriptCreateError = "Could not write the script. See the Log for details.";
			else
			{
				const AssetHandle handle = AssetManager::ImportAsset(path);
				Refresh();
				if (!handle)
					m_ScriptCreateError = "The script was saved but could not be imported. See the Log, then refresh the Content Browser.";
				else
				{
					ChangeDirectory(GetDirectory(std::filesystem::path("Scripts") / "Source"));
					ClearSelections();
					SelectItem(handle);
					m_NewScriptDialogOpen = false;
					ImGui::CloseCurrentPopup();
					ActivateAsset(Project::GetEditorAssetManager()->GetMetadata(handle));
				}
			}
			if (!m_ScriptCreateError.empty())
				LUX_CORE_ERROR_TAG("ScriptEngine", "{} ({})", m_ScriptCreateError, path.string());
		}
		ImGui::EndPopup();
	}

	bool ContentBrowserPanel::CreateNewFolder()
	{
		if (!m_CurrentDirectory)
			return false;

		std::filesystem::path filepath = FileSystem::GetUniqueFileName(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath / "New Folder");
		if (!FileSystem::CreateDirectory(filepath))
			return false;

		Refresh();
		if (Ref<DirectoryInfo> directory = GetDirectory(m_CurrentDirectory->FilePath / filepath.filename()))
		{
			ClearSelections();
			SelectItem(directory->Handle);
			StartRenamingItem(directory->Handle);
		}

		return true;
	}

	AssetHandle ContentBrowserPanel::CreateSceneAsset()
	{
		if (!m_CurrentDirectory)
			return 0;

		const std::string extension = Project::GetEditorAssetManager()->GetDefaultExtensionForAssetType(AssetType::Scene);
		const std::filesystem::path relativePath = std::filesystem::relative(
			FileSystem::GetUniqueFileName(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath / ("New Scene" + extension)),
			Project::GetActiveAssetDirectory()).lexically_normal();

		Ref<Scene> scene = Ref<Scene>::Create();
		scene->SetName(relativePath.stem().string());
		SceneSerializer serializer(scene);
		serializer.Serialize(Project::GetActiveAssetDirectory() / relativePath);

		AssetHandle handle = Project::GetEditorAssetManager()->ImportAsset(relativePath);
		Refresh();
		if (handle)
		{
			ClearSelections();
			SelectItem(handle);
			StartRenamingItem(handle);
		}

		return handle;
	}

	AssetHandle ContentBrowserPanel::CreateAudioSurfaceTable()
	{
		if (!m_CurrentDirectory)
			return 0;
		const auto path = FileSystem::GetUniqueFileName(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath / "New Surface Table.lsurfaces");
		AudioSurfaceTable table;
		std::ofstream file(path);
		file << table.ToYAML();
		file.close();
		if (!file)
		{
			LUX_CORE_ERROR_TAG("Audio", "Could not create surface table '{}'", path.string());
			return 0;
		}
		const AssetHandle handle = AssetManager::ImportAsset(std::filesystem::relative(path, Project::GetActiveAssetDirectory()));
		Refresh();
		if (handle)
		{
			ClearSelections();
			SelectItem(handle);
			StartRenamingItem(handle);
		}
		return handle;
	}

	AssetHandle ContentBrowserPanel::CreateDialogueTable()
	{
		if (!m_CurrentDirectory)
			return 0;
		const auto path = FileSystem::GetUniqueFileName(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath / "New Dialogue Table.ldialogue");
		DialogueTable table;
		std::ofstream file(path);
		file << table.ToYAML();
		file.close();
		if (!file)
		{
			LUX_CORE_ERROR_TAG("Audio", "Could not create dialogue table '{}'", path.string());
			return 0;
		}
		const AssetHandle handle = AssetManager::ImportAsset(std::filesystem::relative(path, Project::GetActiveAssetDirectory()));
		Refresh();
		if (handle)
		{
			ClearSelections();
			SelectItem(handle);
			StartRenamingItem(handle);
		}
		return handle;
	}

	std::vector<AssetHandle> ContentBrowserPanel::GetSoundClips(AssetHandle item) const
	{
		std::vector<AssetHandle> clips;
		if (!m_SelectedItems.IsSelected(item))
			return { item };
		for (size_t index = 0; index < m_SelectedItems.SelectionCount(); ++index)
		{
			const AssetHandle selected = m_SelectedItems[index];
			if (AssetManager::GetAssetType(selected) == AssetType::Audio)
				clips.push_back(selected);
		}
		return clips;
	}

	AssetHandle ContentBrowserPanel::CreateSoundAsset(const std::vector<AssetHandle>& clips)
	{
		if (!m_CurrentDirectory)
			return 0;
		Sound sound;
		std::string name = "New Sound";
		std::vector<AssetHandle> audio;
		for (const AssetHandle clip : clips)
		{
			if (clip && AssetManager::GetAssetType(clip) == AssetType::Audio)
				audio.push_back(clip);
		}
		if (audio.size() == 1)
			sound.Nodes.front().Clip = audio.front();
		else if (audio.size() > 1)
		{
			// A Random container of the clips: the usual way to vary footsteps, impacts and barks.
			sound.Nodes.front().Type = SoundNodeType::Random;
			sound.Nodes.front().AvoidRepeat = true;
			for (const AssetHandle clip : audio)
				sound.Nodes[sound.AddNode(0, SoundNodeType::Clip)].Clip = clip;
		}
		if (!audio.empty())
			name = Project::GetEditorAssetManager()->GetMetadata(audio.front()).FilePath.stem().string();
		std::string error;
		if (!sound.Validate(&error) || sound.ToYAML().size() > Sound::MaxFileBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot make one Sound of {} clips: {}", audio.size(),
				error.empty() ? "it would be larger than the 64 KiB a Sound file may be; select fewer" : error);
			return 0;
		}
		const auto path = FileSystem::GetUniqueFileName(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath / (name + ".lsound"));
		std::ofstream file(path);
		file << sound.ToYAML();
		file.close();
		if (!file)
		{
			LUX_CORE_ERROR_TAG("Audio", "Could not create sound '{}'", path.string());
			return 0;
		}
		const AssetHandle handle = AssetManager::ImportAsset(std::filesystem::relative(path, Project::GetActiveAssetDirectory()));
		Refresh();
		if (handle)
		{
			ClearSelections();
			SelectItem(handle);
			StartRenamingItem(handle);
		}
		return handle;
	}

	AssetHandle ContentBrowserPanel::CreateSoundBankAsset()
	{
		if (!m_CurrentDirectory)
			return 0;
		// Seeded with the selected Sounds, the way New Sound takes the selected clips.
		SoundBank bank;
		for (size_t index = 0; index < m_SelectedItems.SelectionCount(); ++index)
		{
			if (AssetManager::GetAssetType(m_SelectedItems[index]) == AssetType::Sound)
				bank.Add(m_SelectedItems[index]);
		}
		if (bank.ToYAML().size() > SoundBank::MaxFileBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot make one Sound Bank of {} Sounds: it would be larger than the 64 KiB a bank file may be", bank.Sounds.size());
			return 0;
		}
		const auto path = FileSystem::GetUniqueFileName(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath / "New Sound Bank.lbank");
		std::ofstream file(path);
		file << bank.ToYAML();
		file.close();
		if (!file)
		{
			LUX_CORE_ERROR_TAG("Audio", "Could not create sound bank '{}'", path.string());
			return 0;
		}
		const AssetHandle handle = AssetManager::ImportAsset(std::filesystem::relative(path, Project::GetActiveAssetDirectory()));
		Refresh();
		if (handle)
		{
			ClearSelections();
			SelectItem(handle);
			StartRenamingItem(handle);
		}
		return handle;
	}

	AssetHandle ContentBrowserPanel::CreateMaterialInstance(AssetHandle parentHandle)
	{
		Ref<MaterialAsset> parent = AssetManager::GetAsset<MaterialAsset>(parentHandle);
		if (!parent)
			return 0;

		auto assetManager = Project::GetEditorAssetManager();
		const std::string extension = assetManager->GetDefaultExtensionForAssetType(AssetType::Material);
		const std::filesystem::path parentPath = assetManager->GetFileSystemPath(parentHandle);
		const std::filesystem::path relativePath = std::filesystem::relative(
			FileSystem::GetUniqueFileName(parentPath.parent_path() / (parentPath.stem().string() + " Instance" + extension)),
			Project::GetActiveAssetDirectory()).lexically_normal();

		Ref<MaterialAsset> instance = Ref<MaterialAsset>::Create(parent->IsTransparent());
		instance->SetParent(parentHandle);
		instance->InheritFrom(*parent);

		AssetMetadata metadata;
		metadata.Type = AssetType::Material;
		metadata.FilePath = relativePath;
		AssetImporter::Serialize(metadata, instance);

		const AssetHandle handle = assetManager->ImportAsset(relativePath);
		Refresh();
		if (handle)
		{
			LUX_CONSOLE_LOG_INFO("Created material instance '{}'", relativePath.generic_string());
			ClearSelections();
			SelectItem(handle);
			StartRenamingItem(handle);
		}
		return handle;
	}

	AssetHandle ContentBrowserPanel::CreateMaterialAsset()
	{
		if (!m_CurrentDirectory)
			return 0;

		const std::string extension = Project::GetEditorAssetManager()->GetDefaultExtensionForAssetType(AssetType::Material);
		const std::filesystem::path relativePath = std::filesystem::relative(
			FileSystem::GetUniqueFileName(Project::GetActiveAssetDirectory() / m_CurrentDirectory->FilePath / ("New Material" + extension)),
			Project::GetActiveAssetDirectory()).lexically_normal();

		AssetMetadata metadata;
		metadata.Type = AssetType::Material;
		metadata.FilePath = relativePath;

		Ref<MaterialAsset> material = Ref<MaterialAsset>::Create();
		AssetImporter::Serialize(metadata, material);

		AssetHandle handle = Project::GetEditorAssetManager()->ImportAsset(relativePath);
		Refresh();
		if (handle)
		{
			ClearSelections();
			SelectItem(handle);
			StartRenamingItem(handle);
		}

		return handle;
	}

	ContentBrowserItemList ContentBrowserPanel::Search(const std::string& query, const Ref<DirectoryInfo>& directoryInfo) const
	{
		ContentBrowserItemList results;
		std::string queryLower = Utils::String::ToLowerCopy(query);

		for (const auto& [handle, subdir] : directoryInfo->SubDirectories)
		{
			std::string directoryName =
				Utils::String::ToLowerCopy(Utils::PathToUTF8(subdir->FilePath.filename()));
			if (directoryName.find(queryLower) != std::string::npos)
				results.Items.push_back(Ref<ContentBrowserDirectory>::Create(subdir));

			ContentBrowserItemList children = Search(query, subdir);
			results.Items.insert(results.Items.end(), children.Items.begin(), children.Items.end());
		}

		for (AssetHandle assetHandle : directoryInfo->Assets)
		{
			AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(assetHandle);
			std::string filename =
				Utils::String::ToLowerCopy(Utils::PathToUTF8(metadata.FilePath.filename()));
			if (filename.find(queryLower) != std::string::npos)
				results.Items.push_back(Ref<ContentBrowserAsset>::Create(metadata, GetAssetIcon(metadata)));
		}

		return results;
	}

	Ref<Texture2D> ContentBrowserPanel::GetAssetIcon(const AssetMetadata& metadata) const
	{
		std::string extension = Utils::String::ToLowerCopy(metadata.FilePath.extension().string());
		if (auto it = m_AssetIconMap.find(extension); it != m_AssetIconMap.end() && it->second)
			return it->second;

		switch (metadata.Type)
		{
		case AssetType::Scene: return EditorResources::SceneFileIcon;
		case AssetType::Prefab: return EditorResources::PrefabFileIcon;
		case AssetType::Material: return EditorResources::MaterialFileIcon;
		case AssetType::Mesh: return EditorResources::MeshFileIcon;
		case AssetType::StaticMesh: return EditorResources::StaticMeshFileIcon;
		case AssetType::Texture: return EditorResources::FileIcon;
		case AssetType::Audio: return EditorResources::AudioIcon;
		case AssetType::DialogueTable:
		case AssetType::AudioSurfaceTable: return EditorResources::AudioIcon;
		case AssetType::Sound: return EditorResources::SoundConfigFileIcon;
		case AssetType::SoundBank: return EditorResources::SoundGraphFileIcon;
		case AssetType::ScriptFile: return EditorResources::CSFileIcon;
		case AssetType::Font: return EditorResources::FontFileIcon;
		case AssetType::Animation: return EditorResources::AnimationFileIcon;
		case AssetType::AnimationGraph: return EditorResources::AnimationGraphFileIcon;
		case AssetType::MeshCollider: return EditorResources::MeshColliderFileIcon;
		case AssetType::SoundGraphSound: return EditorResources::SoundGraphFileIcon;
		default: return EditorResources::FileIcon;
		}
	}

	Ref<DirectoryInfo> ContentBrowserPanel::GetDirectory(const std::filesystem::path& filepath) const
	{
		if (!m_Project)
			return nullptr;

		std::filesystem::path normalized = filepath.lexically_normal();
		if (normalized.empty() || normalized == "." || normalized == m_Project->GetAssetDirectory())
		{
			for (const auto& [handle, directory] : m_Directories)
			{
				if (directory->FilePath.empty())
					return directory;
			}
		}

		if (normalized.is_absolute())
			normalized = std::filesystem::relative(normalized, m_Project->GetAssetDirectory()).lexically_normal();

		for (const auto& [handle, directory] : m_Directories)
		{
			if (directory->FilePath.lexically_normal() == normalized)
				return directory;
		}

		return nullptr;
	}

	void ContentBrowserPanel::SelectItem(AssetHandle handle)
	{
		m_SelectedItems.Select(handle);
		m_SelectionAnchor = handle;
	}

	void ContentBrowserPanel::DeselectItem(AssetHandle handle)
	{
		m_SelectedItems.Deselect(handle);
		if (m_SelectionAnchor == handle)
			m_SelectionAnchor = m_SelectedItems.SelectionCount() > 0 ? m_SelectedItems[0] : AssetHandle(0);
	}

	void ContentBrowserPanel::ClearSelections()
	{
		for (auto& item : m_CurrentItems.Items)
		{
			if (item->IsRenaming())
				item->StopRenaming();
		}

		m_SelectedItems.Clear();
		m_SelectionAnchor = 0;
	}

	void ContentBrowserPanel::SelectRangeTo(AssetHandle handle)
	{
		if (!m_SelectionAnchor)
		{
			ClearSelections();
			SelectItem(handle);
			return;
		}

		size_t anchorIndex = m_CurrentItems.FindItem(m_SelectionAnchor);
		size_t targetIndex = m_CurrentItems.FindItem(handle);
		if (anchorIndex == ContentBrowserItemList::InvalidItem || targetIndex == ContentBrowserItemList::InvalidItem)
		{
			ClearSelections();
			SelectItem(handle);
			return;
		}

		if (anchorIndex > targetIndex)
			std::swap(anchorIndex, targetIndex);

		m_SelectedItems.Clear();
		for (size_t i = anchorIndex; i <= targetIndex; i++)
			m_SelectedItems.Select(m_CurrentItems[i]->GetID());
	}

	bool ContentBrowserPanel::MoveSelectionTo(const std::filesystem::path& destination)
	{
		bool moved = false;
		std::vector<AssetHandle> selections(m_SelectedItems.begin(), m_SelectedItems.end());
		for (AssetHandle handle : selections)
		{
			size_t index = m_CurrentItems.FindItem(handle);
			if (index == ContentBrowserItemList::InvalidItem)
				continue;

			if (m_CurrentItems[index]->Move(destination))
				moved = true;
		}

		return moved;
	}

	bool ContentBrowserPanel::DeleteDirectory(const Ref<DirectoryInfo>& directory)
	{
		if (!directory)
			return false;

		const std::filesystem::path fullPath = Project::GetActiveAssetDirectory() / directory->FilePath;
		if (!FileSystem::DeleteFile(fullPath))
			return false;

		std::vector<AssetHandle> assetsToRemove;
		std::function<void(const Ref<DirectoryInfo>&)> collectAssets = [&](const Ref<DirectoryInfo>& current)
		{
			assetsToRemove.insert(assetsToRemove.end(), current->Assets.begin(), current->Assets.end());
			for (const auto& [childHandle, childDirectory] : current->SubDirectories)
				collectAssets(childDirectory);
		};
		collectAssets(directory);

		for (AssetHandle handle : assetsToRemove)
			Project::GetEditorAssetManager()->RemoveAsset(handle);

		return true;
	}

	bool ContentBrowserPanel::MoveDirectory(const Ref<DirectoryInfo>& directory, const std::filesystem::path& destination)
	{
		if (!directory)
			return false;

		const std::filesystem::path oldRelativePath = directory->FilePath.lexically_normal();
		const std::filesystem::path newRelativePath = (destination / oldRelativePath.filename()).lexically_normal();
		if (oldRelativePath == newRelativePath)
			return false;

		const std::filesystem::path oldAbsolutePath = Project::GetActiveAssetDirectory() / oldRelativePath;
		const std::filesystem::path newAbsolutePath = Project::GetActiveAssetDirectory() / newRelativePath;
		if (PathStartsWith(newAbsolutePath.lexically_normal(), oldAbsolutePath.lexically_normal()))
			return false;

		if (!FileSystem::MoveFile(oldAbsolutePath, Project::GetActiveAssetDirectory() / destination))
			return false;

		const AssetRegistry& registry = Project::GetEditorAssetManager()->GetAssetRegistry();
		std::vector<AssetMetadata> updatedMetadata;
		for (const auto& [handle, entry] : registry)
		{
			if (!PathStartsWith(entry.FilePath.lexically_normal(), oldRelativePath))
				continue;

			AssetMetadata metadata = entry;
			metadata.FilePath = ReplacePathPrefix(metadata.FilePath.lexically_normal(), oldRelativePath, newRelativePath);
			updatedMetadata.push_back(metadata);
		}

		for (const AssetMetadata& metadata : updatedMetadata)
		{
			AssetMetadata updated = metadata;
			updated.FileLastWriteTime = FileSystem::GetLastWriteTime(Project::GetEditorAssetManager()->GetFileSystemPath(updated));
			Project::GetEditorAssetManager()->SetMetadata(updated.Handle, updated);
		}

		Project::GetEditorAssetManager()->SerializeAssetRegistry();
		return true;
	}

	bool ContentBrowserPanel::RenameDirectory(const Ref<DirectoryInfo>& directory, const std::string& newName)
	{
		if (!directory || newName.empty())
			return false;

		const std::filesystem::path oldRelativePath = directory->FilePath.lexically_normal();
		const std::filesystem::path newRelativePath = (oldRelativePath.parent_path() / newName).lexically_normal();
		if (oldRelativePath == newRelativePath)
			return true;

		std::filesystem::path oldAbsolutePath = Project::GetActiveAssetDirectory() / oldRelativePath;
		std::filesystem::path newAbsolutePath = Project::GetActiveAssetDirectory() / newRelativePath;

		if (Utils::String::EqualsIgnoreCase(oldAbsolutePath.filename().string(), newAbsolutePath.filename().string()))
		{
			std::filesystem::path tempAbsolutePath = oldAbsolutePath.parent_path() / "__lux_content_browser_tmp";
			if (!FileSystem::Rename(oldAbsolutePath, tempAbsolutePath))
				return false;
			oldAbsolutePath = tempAbsolutePath;
		}

		if (!FileSystem::Rename(oldAbsolutePath, newAbsolutePath))
			return false;

		const AssetRegistry& registry = Project::GetEditorAssetManager()->GetAssetRegistry();
		std::vector<AssetMetadata> updatedMetadata;
		for (const auto& [handle, entry] : registry)
		{
			if (!PathStartsWith(entry.FilePath.lexically_normal(), oldRelativePath))
				continue;

			AssetMetadata metadata = entry;
			metadata.FilePath = ReplacePathPrefix(metadata.FilePath.lexically_normal(), oldRelativePath, newRelativePath);
			updatedMetadata.push_back(metadata);
		}

		for (const AssetMetadata& metadata : updatedMetadata)
		{
			AssetMetadata updated = metadata;
			updated.FileLastWriteTime = FileSystem::GetLastWriteTime(Project::GetEditorAssetManager()->GetFileSystemPath(updated));
			Project::GetEditorAssetManager()->SetMetadata(updated.Handle, updated);
		}

		Project::GetEditorAssetManager()->SerializeAssetRegistry();
		return true;
	}

	std::filesystem::path ContentBrowserPanel::TrashPathFor(AssetHandle handle, const std::filesystem::path& originalFileName)
	{
		// Trash lives in the project directory, NOT under Assets/ — ProcessDirectory recurses the asset
		// tree and would otherwise re-import trashed files. The handle prefix keeps names unique.
		return Project::GetActiveProjectDirectory() / ".trash" / (std::to_string((uint64_t)handle) + "__" + originalFileName.string());
	}

	bool ContentBrowserPanel::TrashAsset(AssetHandle handle)
	{
		auto assetManager = Project::GetEditorAssetManager();
		AssetMetadata metadata = assetManager->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		const std::filesystem::path sourcePath = assetManager->GetFileSystemPath(metadata);
		const std::filesystem::path trashPath = TrashPathFor(handle, metadata.FilePath.filename());
		FileSystem::CreateDirectory(trashPath.parent_path());
		if (!FileSystem::Move(sourcePath, trashPath))   // move, never delete — undo restores it
			return false;

		assetManager->RemoveAsset(handle);
		assetManager->SerializeAssetRegistry();
		return true;
	}

	bool ContentBrowserPanel::RestoreAsset(AssetHandle handle, const AssetMetadata& metadata)
	{
		auto assetManager = Project::GetEditorAssetManager();
		const std::filesystem::path originalPath = Project::GetActiveAssetDirectory() / metadata.FilePath;
		const std::filesystem::path trashPath = TrashPathFor(handle, metadata.FilePath.filename());
		if (!FileSystem::Move(trashPath, originalPath))
			return false;

		AssetMetadata restored = metadata;
		restored.FileLastWriteTime = FileSystem::GetLastWriteTime(originalPath);
		assetManager->SetMetadata(handle, restored);   // same handle, so scene references still resolve
		assetManager->SerializeAssetRegistry();
		return true;
	}

	bool ContentBrowserPanel::RawMoveAsset(AssetHandle handle, const std::filesystem::path& destination)
	{
		AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		const std::filesystem::path sourcePath = Project::GetEditorAssetManager()->GetFileSystemPath(metadata);
		const std::filesystem::path newRelativePath = (destination / sourcePath.filename()).lexically_normal();
		if (metadata.FilePath.lexically_normal() == newRelativePath)
			return false;

		if (!FileSystem::MoveFile(sourcePath, Project::GetActiveAssetDirectory() / destination))
			return false;

		metadata.FilePath = newRelativePath;
		metadata.FileLastWriteTime = FileSystem::GetLastWriteTime(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
		Project::GetEditorAssetManager()->SetMetadata(handle, metadata);
		Project::GetEditorAssetManager()->SerializeAssetRegistry();
		return true;
	}

	bool ContentBrowserPanel::RawRenameAsset(AssetHandle handle, const std::string& newName)
	{
		if (newName.empty())
			return false;

		AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		std::filesystem::path sourcePath = Project::GetEditorAssetManager()->GetFileSystemPath(metadata);
		std::filesystem::path destinationPath = sourcePath.parent_path() / (newName + sourcePath.extension().string());

		if (Utils::String::EqualsIgnoreCase(sourcePath.filename().string(), destinationPath.filename().string()))
		{
			std::filesystem::path tempPath = sourcePath.parent_path() / ("__lux_cb_tmp" + sourcePath.extension().string());
			if (!FileSystem::Rename(sourcePath, tempPath))
				return false;
			sourcePath = tempPath;
		}

		if (!FileSystem::Rename(sourcePath, destinationPath))
			return false;

		metadata.FilePath = (metadata.FilePath.parent_path() / destinationPath.filename()).lexically_normal();
		metadata.FileLastWriteTime = FileSystem::GetLastWriteTime(destinationPath);
		Project::GetEditorAssetManager()->SetMetadata(handle, metadata);
		Project::GetEditorAssetManager()->SerializeAssetRegistry();
		return true;
	}

	bool ContentBrowserPanel::DeleteAsset(AssetHandle handle)
	{
		AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		if (!TrashAsset(handle))
			return false;

		if (m_UndoPush)
		{
			m_UndoPush("Delete Asset",
				[handle, metadata]() { RestoreAsset(handle, metadata); Get().Refresh(); },
				[handle]() { TrashAsset(handle); Get().Refresh(); });
		}
		return true;
	}

	bool ContentBrowserPanel::MoveAsset(AssetHandle handle, const std::filesystem::path& destination)
	{
		AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		const std::filesystem::path originalDir = metadata.FilePath.parent_path();
		if (!RawMoveAsset(handle, destination))
			return false;

		if (m_UndoPush)
		{
			m_UndoPush("Move Asset",
				[handle, originalDir]() { RawMoveAsset(handle, originalDir); Get().Refresh(); },
				[handle, destination]() { RawMoveAsset(handle, destination); Get().Refresh(); });
		}
		return true;
	}

	bool ContentBrowserPanel::RenameAsset(AssetHandle handle, const std::string& newName)
	{
		AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		const std::string oldName = Project::GetEditorAssetManager()->GetFileSystemPath(metadata).stem().string();
		if (!RawRenameAsset(handle, newName))
			return false;

		if (m_UndoPush)
		{
			m_UndoPush("Rename Asset",
				[handle, oldName]() { RawRenameAsset(handle, oldName); Get().Refresh(); },
				[handle, newName]() { RawRenameAsset(handle, newName); Get().Refresh(); });
		}
		return true;
	}

	Ref<Texture2D> ContentBrowserPanel::GetItemThumbnail(AssetHandle handle)
	{
		if (!m_ThumbnailCache || !IsThumbnailSupported(AssetManager::GetAssetType(handle)))
			return nullptr;

		const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return nullptr;

		Ref<Texture2D> thumbnail = m_ThumbnailCache->GetOrCreateThumbnail(metadata.FilePath);

		// The cache only generates image thumbnails itself; materials are rendered, and audio files
		// and Sounds drawn from their waveforms.
		if (!thumbnail && metadata.Type == AssetType::Material)
		{
			if (!m_MaterialThumbnailer)
				m_MaterialThumbnailer = Ref<MaterialThumbnailer>::Create();
			m_MaterialThumbnailer->Request(handle);
		}
		if (!thumbnail && (metadata.Type == AssetType::Audio || metadata.Type == AssetType::Sound))
		{
			if (!m_WaveformThumbnailer)
				m_WaveformThumbnailer = Ref<WaveformThumbnailer>::Create();
			m_WaveformThumbnailer->Request(handle);
		}

		return thumbnail;
	}

}
