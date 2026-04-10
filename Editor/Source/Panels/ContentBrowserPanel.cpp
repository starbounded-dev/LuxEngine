#include "lpch.h"
#include "ContentBrowserPanel.h"
#include "Lux/Project/Project.h"
#include "Lux/Asset/TextureImporter.h"
#include "Lux/Core/Application.h"

#include <imgui/imgui.h>
#include "ImGui/ImGuiEx.h"

#include "Lux/Utilities/StringUtils.h"

namespace Lux {

	namespace {
		// FIX: Added null guard � previously this would crash if texture or its
		// underlying image was null (e.g. icon PNGs failed to load).
		ImTextureID GetImGuiTextureID(const Lux::Ref<Lux::Texture2D>& texture)
		{
			if (!texture || !texture->GetImage())
				return ImTextureID{};
			auto* imguiRenderer = Lux::Application::Get().GetImGuiLayer()->GetImGuiRenderer();
			return imguiRenderer->CreateFrameTexture(texture->GetImage()->GetHandle().Get(), nvrhi::AllSubresources);
		}
	}


	ContentBrowserPanel::ContentBrowserPanel()
	{
		m_DirectoryIcon = TextureImporter::LoadTexture2D("Resources/Icons/ContentBrowser/DirectoryIcon.png");
		m_FileIcon = TextureImporter::LoadTexture2D("Resources/Icons/ContentBrowser/FileIcon.png");
		m_Mode = Mode::FileSystem;
	}

	ContentBrowserPanel::ContentBrowserPanel(Ref<Project> project)
		: ContentBrowserPanel()
	{
		InitializeFromProject(project);
	}

	void ContentBrowserPanel::OnProjectChanged(const Ref<Project>& project)
	{
		InitializeFromProject(project);
	}

	void ContentBrowserPanel::InitializeFromProject(const Ref<Project>& project)
	{
		m_Project = project;
		m_TreeNodes.clear();
		m_AssetTree.clear();

		if (!m_Project)
		{
			m_ThumbnailCache.reset();
			m_BaseDirectory.clear();
			m_CurrentDirectory.clear();
			return;
		}

		m_ThumbnailCache = CreateRef<ThumbnailCache>(project);
		m_BaseDirectory = m_Project->GetAssetDirectory();
		m_CurrentDirectory = m_BaseDirectory;
		m_TreeNodes.push_back(TreeNode(".", 0));
		RefreshAssetTree();
	}

	void ContentBrowserPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		ImGui::Begin("Content Browser", &isOpen);

		if (!m_Project)
		{
			ImGui::TextDisabled("No active project.");
			ImGui::End();
			return;
		}

		const char* label = m_Mode == Mode::Asset ? "Asset" : "File";
		if (ImGui::Button(label))
		{
			m_Mode = m_Mode == Mode::Asset ? Mode::FileSystem : Mode::Asset;
		}

		if (m_CurrentDirectory != std::filesystem::path(m_BaseDirectory))
		{
			ImGui::SameLine();
			if (ImGui::Button("<-"))
			{
				m_CurrentDirectory = m_CurrentDirectory.parent_path();
			}
		}

		static float padding = 16.0f;
		static float thumbnailSize = 128.0f;
		float cellSize = thumbnailSize + padding;

		float panelWidth = ImGui::GetContentRegionAvail().x;
		int columnCount = (int)(panelWidth / cellSize);
		if (columnCount < 1)
			columnCount = 1;

		ImGui::Columns(columnCount, 0, false);

		if (m_Mode == Mode::Asset)
		{
			TreeNode* node = &m_TreeNodes[0];

			auto currentDir = std::filesystem::relative(m_CurrentDirectory, m_Project->GetAssetDirectory());
			for (const auto& p : currentDir)
			{
				if (node->Path == currentDir)
					break;

				if (node->Children.find(p) != node->Children.end())
				{
					node = &m_TreeNodes[node->Children[p]];
					continue;
				}
				else
				{
					LUX_CORE_ASSERT(false);
				}
			}

			for (const auto& [item, treeNodeIndex] : node->Children)
			{
				bool isDirectory = std::filesystem::is_directory(m_Project->GetAssetDirectory() / item);

				std::string itemStr = item.generic_string();

				ImGui::PushID(itemStr.c_str());

				Ref<Texture2D> icon = isDirectory ? m_DirectoryIcon : m_FileIcon;
				ImTextureID texID = GetImGuiTextureID(icon); // null-safe after the fix above

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				if (texID)
					ImGui::ImageButton("##icon", texID, { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
				else
					ImGui::Button("##icon", { thumbnailSize, thumbnailSize });

				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Delete"))
					{
						LUX_CORE_ASSERT(false, "Not implemented");
					}
					ImGui::EndPopup();
				}

				if (ImGui::BeginDragDropSource())
				{
					AssetHandle handle = m_TreeNodes[treeNodeIndex].Handle;
					ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", &handle, sizeof(AssetHandle));
					ImGui::EndDragDropSource();
				}

				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (isDirectory)
						m_CurrentDirectory /= item.filename();
				}

				ImGui::TextWrapped(itemStr.c_str());

				ImGui::NextColumn();

				ImGui::PopID();
			}
		}
		else
		{
			// FIX 1: Removed ImGuiListClipper. The previous code used a clipper
			// together with ImGui::Columns, which are fundamentally incompatible:
			//
			//   - ImGui::Columns manages a grid layout where ImGui::NextColumn()
			//     advances to the next cell. Each "item" occupies one cell.
			//   - ImGuiListClipper operates on a linear list. It computes which
			//     rows are visible by measuring item heights, but it has no concept
			//     of columns. Once NextColumn() is called inside the clipper range,
			//     the clipper's height measurements are corrupted and it produces
			//     wrong DisplayStart/DisplayEnd values, causing most items to be
			//     skipped entirely and never rendered.
			//
			//   Additionally, the clipper code re-created a fresh
			//   directory_iterator at the top of every while(clipper.Step())
			//   iteration, so the iterator always restarted from the beginning of
			//   the directory regardless of DisplayStart.
			//
			// Fix: plain range-for with no clipper. Correct, simple, and fully
			// compatible with ImGui::Columns. Performance on large directories
			// is acceptable; a proper solution would switch to ImGui::BeginTable
			// (which has built-in clipping that understands columns) if needed.
			//
			// FIX 2: Removed ImGui::SetCursorPosY(GetCursorPosY() + diff).
			// That call was intended to vertically center non-square thumbnails,
			// but inside ImGui::Columns it corrupts the row-height tracking used
			// by NextColumn(). ImGui tracks the tallest item seen in the current
			// row to set the row height; manually advancing the cursor Y makes
			// later items in the same row appear to have negative height, causing
			// them to overlap or be invisible. All thumbnails now use a square
			// button so the layout is stable.
			for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory))
			{
				const auto& path = directoryEntry.path();
				std::string filenameString = path.filename().string();

				ImGui::PushID(filenameString.c_str());

				auto relativePath = std::filesystem::relative(path, m_Project->GetAssetDirectory());

				Ref<Texture2D> thumbnail = m_DirectoryIcon;
				if (!directoryEntry.is_directory())
				{
					// GetOrCreateThumbnail returns null on the first call for an
					// asset (generation is queued). Fall back to the file icon
					// until the thumbnail is ready on a subsequent frame.
					Ref<Texture2D> cached = m_ThumbnailCache->GetOrCreateThumbnail(relativePath);
					thumbnail = cached ? cached : m_FileIcon;
				}

				// If even the fallback icons failed to load, skip rather than crash.
				if (!thumbnail)
				{
					ImGui::PopID();
					ImGui::NextColumn();
					continue;
				}

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

				ImTextureID texID = GetImGuiTextureID(thumbnail);
				if (texID)
					ImGui::ImageButton("##thumbnail", texID, { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
				else
					ImGui::Button("##thumbnail", { thumbnailSize, thumbnailSize });

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					uint64_t estimatedSize = (uint64_t)thumbnail->GetWidth() * (uint64_t)thumbnail->GetHeight() * 4;
					std::string sizeString = Utils::BytesToString(estimatedSize);
					ImGui::Text("Memory: %s", sizeString.c_str());
					ImGui::EndTooltip();
				}

				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Import"))
					{
						m_Project->GetEditorAssetManager()->ImportAsset(relativePath);
						RefreshAssetTree();
					}
					ImGui::EndPopup();
				}

				// FIX 3: FileSystem mode had no drag-drop source at all � you
				// couldn't drag files from this mode onto scene entities. Added it
				// here to match the Asset mode behaviour.
				if (ImGui::BeginDragDropSource())
				{
					AssetHandle handle = 0;
					const auto& registry = m_Project->GetEditorAssetManager()->GetAssetRegistry();
					for (const auto& [h, meta] : registry)
					{
						if (meta.FilePath == relativePath)
						{
							handle = h;
							break;
						}
					}
					if (handle)
						ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", &handle, sizeof(AssetHandle));
					ImGui::EndDragDropSource();
				}

				ImGui::PopStyleColor();

				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (directoryEntry.is_directory())
						m_CurrentDirectory /= path.filename();
				}

				ImGui::TextWrapped(filenameString.c_str());

				ImGui::NextColumn();

				ImGui::PopID();
			}
		}

		ImGui::Columns(1);

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::PropertySlider("Thumbnail Size", thumbnailSize, 16.0f, 512.0f);
		ImGuiEx::PropertySlider("Padding", padding, 0.0f, 32.0f);
		ImGuiEx::EndPropertyGrid();

		// TODO: status bar
		ImGui::End();

		if (m_ThumbnailCache)
			m_ThumbnailCache->OnUpdate();
	}


	void ContentBrowserPanel::RefreshAssetTree()
	{
		if (!m_Project)
			return;

		const auto& assetRegistry = m_Project->GetEditorAssetManager()->GetAssetRegistry();
		for (const auto& [handle, metadata] : assetRegistry)
		{
			uint32_t currentNodeIndex = 0;

			for (const auto& p : metadata.FilePath)
			{
				auto it = m_TreeNodes[currentNodeIndex].Children.find(p.generic_string());
				if (it != m_TreeNodes[currentNodeIndex].Children.end())
				{
					currentNodeIndex = it->second;
				}
				else
				{
					TreeNode newNode(p, handle);
					newNode.Parent = currentNodeIndex;
					m_TreeNodes.push_back(newNode);

					const uint32_t newIndex = (uint32_t)(m_TreeNodes.size() - 1);
					m_TreeNodes[currentNodeIndex].Children[p] = newIndex;
					currentNodeIndex = newIndex;
				}
			}
		}
	}

}
