#include "lpch.h"
#include "ContentBrowserPanel.h"

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
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
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
			AssetType::EnvMap
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

		if (!m_Project || !m_BaseDirectory)
		{
			ImGui::TextDisabled("No active project.");
			ImGui::End();
			return;
		}

		ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable
			| ImGuiTableFlags_SizingFixedFit
			| ImGuiTableFlags_BordersInnerV;

		if (ImGui::BeginTable("##ContentBrowserLayout", 2, tableFlags))
		{
			ImGui::TableSetupColumn("Outliner", 0, 280.0f);
			ImGui::TableSetupColumn("Directory Structure", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::BeginChild("##content_browser_folders");
			{
				std::vector<Ref<DirectoryInfo>> directories;
				directories.reserve(m_BaseDirectory->SubDirectories.size());
				for (const auto& [handle, directory] : m_BaseDirectory->SubDirectories)
					directories.emplace_back(directory);

				std::sort(directories.begin(), directories.end(), [](const Ref<DirectoryInfo>& a, const Ref<DirectoryInfo>& b)
				{
					return Utils::String::ToLowerCopy(a->FilePath.filename().string()) < Utils::String::ToLowerCopy(b->FilePath.filename().string());
				});

				for (const auto& directory : directories)
					RenderDirectoryHierarchy(directory);
			}
			ImGui::EndChild();

			ImGui::TableSetColumnIndex(1);

			const float topBarHeight = 32.0f;
			const float bottomBarHeight = 28.0f;

			ImGui::BeginChild("##content_browser_main");
			{
				RenderTopBar(topBarHeight);
				ImGui::Separator();

				ImGui::BeginChild("##content_browser_items");
				{
					if (ImGui::BeginPopupContextWindow("##ContentBrowserBackgroundContext", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
					{
						if (ImGui::BeginMenu("New"))
						{
							if (ImGui::MenuItem("Folder"))
								CreateNewFolder();

							if (ImGui::MenuItem("Scene"))
								CreateSceneAsset();

							if (ImGui::MenuItem("Material"))
								CreateMaterialAsset();

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
					const float cellSize = m_ThumbnailSize + 2.0f + paddingForOutline;
					int columnCount = static_cast<int>(panelWidth / cellSize);
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

				RenderBottomBar(bottomBarHeight);
			}
			ImGui::EndChild();

			ImGui::EndTable();
		}

		ImGui::End();

		if (m_ThumbnailCache)
			m_ThumbnailCache->OnUpdate();
	}

	void ContentBrowserPanel::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event) { return OnKeyPressedEvent(event); });
		dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& event) { return OnMouseButtonPressed(event); });
	}

	void ContentBrowserPanel::OnProjectChanged(const Ref<Project>& project)
	{
		m_Project = project;
		m_CurrentItems.Clear();
		m_Directories.clear();
		m_BaseDirectory.reset();
		m_CurrentDirectory.reset();
		m_NextDirectory.reset();
		m_PreviousDirectory.reset();
		m_ThumbnailCache.reset();
		m_BreadCrumbData.clear();
		ClearSelections();
		memset(m_SearchBuffer, 0, MAX_INPUT_BUFFER_LENGTH);

		if (!project)
			return;

		m_ThumbnailCache = Ref<ThumbnailCache>::Create(project);

		AssetHandle baseDirectoryHandle = ProcessDirectory(project->GetAssetDirectory(), nullptr);
		m_BaseDirectory = m_Directories[baseDirectoryHandle];
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

		for (const auto& entry : std::filesystem::directory_iterator(directoryPath))
		{
			if (entry.is_directory())
			{
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

	void ContentBrowserPanel::ChangeDirectory(const Ref<DirectoryInfo>& directory)
	{
		if (!directory)
			return;

		m_UpdateNavigationPath = true;
		m_CurrentItems.Items.clear();

		if (strlen(m_SearchBuffer) == 0)
		{
			for (const auto& [subdirHandle, subdir] : directory->SubDirectories)
				m_CurrentItems.Items.push_back(Ref<ContentBrowserDirectory>::Create(subdir));

			for (AssetHandle assetHandle : directory->Assets)
			{
				AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(assetHandle);
				if (!metadata.IsValid())
					continue;

				m_CurrentItems.Items.push_back(Ref<ContentBrowserAsset>::Create(metadata, GetAssetIcon(metadata)));
			}
		}
		else
		{
			m_CurrentItems = Search(m_SearchBuffer, directory);
		}

		SortItemList();
		m_PreviousDirectory = m_CurrentDirectory;
		m_CurrentDirectory = directory;
		m_NextDirectory.reset();
		ClearSelections();
	}

	void ContentBrowserPanel::OnBrowseBack()
	{
		if (!m_CurrentDirectory || !m_CurrentDirectory->Parent)
			return;

		m_NextDirectory = m_CurrentDirectory;
		ChangeDirectory(m_CurrentDirectory->Parent);
	}

	void ContentBrowserPanel::OnBrowseForward()
	{
		if (m_NextDirectory)
			ChangeDirectory(m_NextDirectory);
	}

	void ContentBrowserPanel::RenderDirectoryHierarchy(const Ref<DirectoryInfo>& directory)
	{
		const bool isSelected = m_CurrentDirectory && m_CurrentDirectory->Handle == directory->Handle;
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (isSelected)
			flags |= ImGuiTreeNodeFlags_Selected;
		if (directory->SubDirectories.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;

		std::string label = directory->FilePath.filename().string();
		if (label.empty())
			label = m_Project->GetConfig().AssetDirectory.string();

		const bool opened = ImGui::TreeNodeEx(reinterpret_cast<const void*>(static_cast<uint64_t>(directory->Handle)), flags, "%s", label.c_str());
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			ChangeDirectory(directory);

		UpdateDropArea(directory);

		if (opened)
		{
			std::vector<Ref<DirectoryInfo>> directories;
			directories.reserve(directory->SubDirectories.size());
			for (const auto& [handle, subdir] : directory->SubDirectories)
				directories.emplace_back(subdir);

			std::sort(directories.begin(), directories.end(), [](const Ref<DirectoryInfo>& a, const Ref<DirectoryInfo>& b)
			{
				return Utils::String::ToLowerCopy(a->FilePath.filename().string()) < Utils::String::ToLowerCopy(b->FilePath.filename().string());
			});

			for (const auto& subdir : directories)
				RenderDirectoryHierarchy(subdir);

			ImGui::TreePop();
		}
	}

	void ContentBrowserPanel::RenderTopBar(float height)
	{
		ImGui::BeginChild("##content_browser_topbar", ImVec2(0.0f, height));

		auto toolbarButton = [](const char* label, const Ref<Texture2D>& icon)
		{
			const bool clicked = ImGui::InvisibleButton(label, ImVec2(22.0f, 22.0f));
			if (icon)
			{
				ImGuiEx::DrawButtonImage(icon,
					IM_COL32(170, 170, 170, 200),
					IM_COL32(220, 220, 220, 255),
					IM_COL32(140, 140, 140, 255),
					ImGuiEx::RectExpanded(ImGuiEx::GetItemRect(), -3.0f, -3.0f));
			}
			return clicked;
		};

		if (toolbarButton("##Back", EditorResources::BackIcon))
			OnBrowseBack();
		ImGuiEx::SetTooltip("Back");

		ImGui::SameLine();
		if (toolbarButton("##Forward", EditorResources::ForwardIcon))
			OnBrowseForward();
		ImGuiEx::SetTooltip("Forward");

		ImGui::SameLine();
		if (toolbarButton("##Refresh", EditorResources::RefreshIcon))
			Refresh();
		ImGuiEx::SetTooltip("Refresh");

		ImGui::SameLine();
		if (toolbarButton("##ClearThumbs", EditorResources::ClearIcon) && m_ThumbnailCache)
			m_ThumbnailCache->Clear();
		ImGuiEx::SetTooltip("Clear thumbnail cache");

		ImGui::SameLine(0.0f, 12.0f);
		ImGui::SetNextItemWidth(220.0f);
		ImGuiEx::Widgets::SearchWidget<MAX_INPUT_BUFFER_LENGTH>(m_SearchBuffer, "Search assets...", &m_FocusSearchWidget);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (m_CurrentDirectory)
				ChangeDirectory(m_CurrentDirectory);
		}

		if (m_UpdateNavigationPath)
		{
			m_BreadCrumbData.clear();
			Ref<DirectoryInfo> current = m_CurrentDirectory;
			while (current && current->Parent)
			{
				m_BreadCrumbData.push_back(current);
				current = current->Parent;
			}

			std::reverse(m_BreadCrumbData.begin(), m_BreadCrumbData.end());
			m_UpdateNavigationPath = false;
		}

		ImGui::SameLine(0.0f, 16.0f);

		std::string rootLabel = m_Project->GetConfig().AssetDirectory.string();
		if (ImGui::SmallButton(rootLabel.c_str()))
			ChangeDirectory(m_BaseDirectory);
		UpdateDropArea(m_BaseDirectory);

		for (const auto& directory : m_BreadCrumbData)
		{
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextUnformatted("/");
			ImGui::SameLine(0.0f, 4.0f);

			std::string directoryName = directory->FilePath.filename().string();
			if (ImGui::SmallButton(directoryName.c_str()))
				ChangeDirectory(directory);
			UpdateDropArea(directory);
		}

		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.0f);
		if (ImGuiEx::Widgets::OptionsButton())
			ImGui::OpenPopup("ContentBrowserSettings");

		if (ImGui::BeginPopup("ContentBrowserSettings"))
		{
			bool saveSettings = false;
			if (ImGui::Checkbox("Show Asset Types", &m_ShowAssetType))
				saveSettings = true;

			if (ImGui::SliderFloat("Thumbnail Size", &m_ThumbnailSize, 96.0f, 256.0f, "%.0f"))
				saveSettings = true;

			if (saveSettings)
				SaveSettings();

			ImGui::EndPopup();
		}

		ImGui::EndChild();
	}

	void ContentBrowserPanel::RenderItems()
	{
		m_IsAnyItemHovered = false;

		for (auto& item : m_CurrentItems.Items)
		{
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
		}

		if (m_OpenDeletePopup)
		{
			ImGui::OpenPopup("Delete");
			m_OpenDeletePopup = false;
		}
	}

	void ContentBrowserPanel::RenderBottomBar(float height)
	{
		ImGui::BeginChild("##content_browser_bottombar", ImVec2(0.0f, height));

		if (GetSelectionCount() == 1)
		{
			AssetHandle selection = GetSelection(0);
			if (m_Directories.contains(selection))
			{
				std::string path = "Assets/" + m_Directories.at(selection)->FilePath.generic_string();
				ImGui::TextUnformatted(path.c_str());
			}
			else
			{
				const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(selection);
				std::string path = "Assets/" + metadata.FilePath.generic_string();
				ImGui::TextUnformatted(path.c_str());
			}
		}
		else if (GetSelectionCount() > 1)
		{
			ImGui::Text("%llu items selected", static_cast<unsigned long long>(GetSelectionCount()));
		}

		ImGui::EndChild();
	}

	void ContentBrowserPanel::Refresh()
	{
		if (!m_Project)
			return;

		std::filesystem::path currentPath;
		if (m_CurrentDirectory)
			currentPath = m_CurrentDirectory->FilePath;

		m_CurrentItems.Clear();
		m_Directories.clear();

		AssetHandle baseDirectoryHandle = ProcessDirectory(m_Project->GetAssetDirectory(), nullptr);
		m_BaseDirectory = m_Directories[baseDirectoryHandle];
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

	void ContentBrowserPanel::SortItemList()
	{
		std::sort(m_CurrentItems.begin(), m_CurrentItems.end(), [](const Ref<ContentBrowserItem>& a, const Ref<ContentBrowserItem>& b)
		{
			if (a->GetType() == b->GetType())
				return Utils::String::ToLowerCopy(a->GetName()) < Utils::String::ToLowerCopy(b->GetName());

			return static_cast<uint16_t>(a->GetType()) < static_cast<uint16_t>(b->GetType());
		});
	}

	void ContentBrowserPanel::LoadSettings()
	{
		auto& settings = Application::Get().GetSettings();
		m_ThumbnailSize = settings.GetFloat("ContentBrowser.ThumbnailSize", 128.0f);
		m_ShowAssetType = settings.GetInt("ContentBrowser.ShowAssetTypes", 1) != 0;
	}

	void ContentBrowserPanel::SaveSettings() const
	{
		auto& settings = Application::Get().GetSettings();
		settings.SetFloat("ContentBrowser.ThumbnailSize", m_ThumbnailSize);
		settings.SetInt("ContentBrowser.ShowAssetTypes", m_ShowAssetType ? 1 : 0);
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
			std::string directoryName = Utils::String::ToLowerCopy(subdir->FilePath.filename().string());
			if (directoryName.find(queryLower) != std::string::npos)
				results.Items.push_back(Ref<ContentBrowserDirectory>::Create(subdir));

			ContentBrowserItemList children = Search(query, subdir);
			results.Items.insert(results.Items.end(), children.Items.begin(), children.Items.end());
		}

		for (AssetHandle assetHandle : directoryInfo->Assets)
		{
			AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(assetHandle);
			std::string filename = Utils::String::ToLowerCopy(metadata.FilePath.filename().string());
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

	bool ContentBrowserPanel::DeleteAsset(AssetHandle handle)
	{
		AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		if (!FileSystem::DeleteFile(Project::GetEditorAssetManager()->GetFileSystemPath(metadata)))
			return false;

		Project::GetEditorAssetManager()->RemoveAsset(handle);
		return true;
	}

	bool ContentBrowserPanel::MoveAsset(AssetHandle handle, const std::filesystem::path& destination)
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

	bool ContentBrowserPanel::RenameAsset(AssetHandle handle, const std::string& newName)
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

	Ref<Texture2D> ContentBrowserPanel::GetItemThumbnail(AssetHandle handle)
	{
		if (!m_ThumbnailCache || !IsThumbnailSupported(AssetManager::GetAssetType(handle)))
			return nullptr;

		const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return nullptr;

		return m_ThumbnailCache->GetOrCreateThumbnail(metadata.FilePath);
	}

}
