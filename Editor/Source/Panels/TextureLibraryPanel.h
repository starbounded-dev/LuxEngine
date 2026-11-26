// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Tools/TextureLibrary.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/JobSystem.h"
#include "Lux/Editor/EditorPanel.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiLayer.h"

#include <atomic>
#include <deque>
#include <format>
#include <functional>
#include <optional>
#include <set>
#include <unordered_map>

namespace Lux {

	namespace TextureLibrary {

		inline std::optional<MaterialRequest> AcceptMaterialDrop()
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PayloadType);
			if (!payload || !payload->IsDelivery() || payload->DataSize != sizeof(MaterialRequest))
				return {};
			MaterialRequest request;
			std::memcpy(&request, payload->Data, sizeof(request));
			if (request.Id.back() != 0 || !ValidId(request.Id.data()))
				return {};
			return request;
		}

	}

	class TextureLibraryPanel : public EditorPanel
	{
	public:
		using OnImported = std::function<void(AssetHandle)>;

		void OnProjectChanged(const Ref<Project>& project) override
		{
			m_Project = project;
			m_Download = nullptr;
			m_OnImported = {};
			m_Status.clear();
			m_Error.clear();
		}

		void StartDownload(const TextureLibrary::MaterialRequest& request, OnImported onImported = {})
		{
			if (m_Download)
			{
				m_Error = "Wait for the current material download to finish.";
				return;
			}
			if (!m_Project || Project::GetActive() != m_Project)
			{
				m_Error = "Open an editor project to import a material.";
				return;
			}
			if (!EnsureWorkers())
				return;
			m_Download = Ref<Job>::Create();
			m_OnImported = std::move(onImported);
			m_Error.clear();
			m_Status = std::format("Downloading {} at {}K", request.Id.data(), request.Resolution);
			const auto assets = Project::GetActiveAssetDirectory();
			JobSystem::Submit([job = m_Download, request, assets]() mutable
			{
				job->Request = request;
				job->Success = TextureLibrary::FetchPack(assets, request, job->Error);
				job->Done.store(true, std::memory_order_release);
			});
		}

		// Called on the main thread even if the panel closed during the download.
		void Update()
		{
			if (m_Index && m_Index->Done.load(std::memory_order_acquire))
			{
				if (m_Index->Success)
				{
					m_Entries = std::move(m_Index->Entries);
					m_ListSource = m_IndexSource;
					m_Status = std::format("{} textures", m_Entries.size());
				}
				else
					m_Error = "Could not load the texture index: " + m_Index->Error;
				m_Index = nullptr;
			}
			if (m_Download && m_Download->Done.load(std::memory_order_acquire))
			{
				const Ref<Job> job = m_Download;
				m_Download = nullptr;
				auto onImported = std::move(m_OnImported);
				if (m_Project && Project::GetActive() == m_Project)
				{
					const AssetHandle handle = job->Success ? TextureLibrary::ImportPack(job->Request) : AssetHandle(0);
					if (handle)
					{
						m_Error.clear();
						m_Status = std::format("Imported {}", job->Request.Id.data());
						if (onImported)
							onImported(handle);
					}
					else
						m_Error = job->Success ? "Could not import the downloaded material." : "Download failed: " + job->Error;
				}
			}
			if (m_Preview && m_Preview->Done.load(std::memory_order_acquire))
			{
				Ref<Texture2D> image;
				if (m_Preview->Success)
				{
					TextureSpecification spec;
					spec.Format = ImageFormat::SRGBA;
					spec.Width = m_Preview->Width;
					spec.Height = m_Preview->Height;
					spec.GenerateMips = false;
					spec.DebugName = m_Preview->Key;
					image = Texture2D::Create(spec, Buffer(m_Preview->Pixels.data(), m_Preview->Pixels.size()));
				}
				if (m_Thumbnails.size() >= 128)
				{
					m_Thumbnails.erase(m_ThumbnailOrder.front());
					m_ThumbnailOrder.pop_front();
				}
				m_ThumbnailOrder.push_back(m_Preview->Key);
				m_Thumbnails[m_Preview->Key] = std::move(image);
				m_Preview = nullptr;
			}
		}

		void OnImGuiRender(bool& isOpen) override
		{
			if (!isOpen)
				return;
			if (!ImGui::Begin("Texture Library", &isOpen))
			{
				ImGui::End();
				return;
			}
			static const char* sources[] = { "Poly Haven", "ambientCG" };
			static const char* sizes[] = { "1K", "2K", "4K" };
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
			if (ImGui::Combo("Source", &m_Source, sources, 2))
				m_Category.clear();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
			ImGui::Combo("Resolution", &m_Resolution, sizes, 3);
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
			const bool search = ImGui::InputTextWithHint("##TextureSearch", "Name or tag", m_Query.data(), m_Query.size(), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			ImGui::BeginDisabled(m_Index != nullptr);
			if (ImGui::Button("Search") || (search && !m_Index))
				StartIndex();
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("CC0");
			ImGui::SameLine();
			ImGui::TextLinkOpenURL("License", m_Source == 0 ? "https://polyhaven.com/license" : "https://docs.ambientcg.com/license/");

			if (!m_Error.empty())
				ImGui::TextWrapped("%s", m_Error.c_str());
			else if (!m_Status.empty())
				ImGui::TextDisabled("%s", m_Status.c_str());
			if (m_ListSource == m_Source)
				DrawResults();
			ImGui::End();
		}

	private:
		struct Job : RefCounted
		{
			std::atomic<bool> Done{ false };
			bool Success = false;
			std::string Error;
			std::vector<TextureLibrary::Entry> Entries;
			TextureLibrary::MaterialRequest Request;
			std::string Key;
			std::vector<uint8_t> Pixels;
			int Width = 0;
			int Height = 0;
		};

		bool EnsureWorkers()
		{
			if (JobSystem::IsMultithreaded())
				return true;
			m_Error = "Texture Library needs the multithreaded editor setting for background downloads.";
			return false;
		}

		static std::string Lower(std::string text)
		{
			for (char& c : text)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return text;
		}

		void StartIndex()
		{
			if (!EnsureWorkers())
				return;
			m_Index = Ref<Job>::Create();
			m_IndexSource = m_Source;
			m_Error.clear();
			m_Status = "Loading texture index";
			JobSystem::Submit([job = m_Index, source = m_Source, query = std::string(m_Query.data())]() mutable
			{
				job->Success = source == 0 ? TextureLibrary::ListPolyHaven(job->Entries, job->Error)
					: TextureLibrary::ListAmbientCG(query, job->Entries, job->Error);
				std::sort(job->Entries.begin(), job->Entries.end(), [](const auto& a, const auto& b) { return a.Name < b.Name; });
				job->Done.store(true, std::memory_order_release);
			});
		}

		void StartPreview(const TextureLibrary::Entry& entry, const std::string& key)
		{
			if (m_Preview || entry.Thumbnail.empty() || !JobSystem::IsMultithreaded())
				return;
			m_Preview = Ref<Job>::Create();
			m_Preview->Key = key;
			JobSystem::Submit([job = m_Preview, url = entry.Thumbnail]() mutable
			{
				const auto path = TextureLibrary::CacheDirectory() / "Thumbnails" / (job->Key + ".png");
				std::vector<uint8_t> bytes;
				const bool cached = TextureLibrary::ReadCache(path, bytes, 1024 * 1024);
				if (cached || TextureLibrary::HttpGet(url, bytes, job->Error))
				{
					int components = 0;
					if (bytes.size() <= 1024 * 1024 && stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &job->Width, &job->Height, &components) &&
						job->Width > 0 && job->Height > 0 && job->Width <= 512 && job->Height <= 512)
					{
						uint8_t* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &job->Width, &job->Height, &components, STBI_rgb_alpha);
						if (pixels)
						{
							job->Pixels.assign(pixels, pixels + size_t(job->Width) * job->Height * 4);
							stbi_image_free(pixels);
							job->Success = true;
							if (!cached)
							{
								std::error_code ec;
								std::filesystem::create_directories(path.parent_path(), ec);
								if (ec || !FileSystem::WriteFileAtomically(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), true))
									LUX_CORE_WARN_TAG("Editor", "[TextureLibrary] could not cache thumbnail {}", job->Key);
							}
						}
					}
				}
				job->Done.store(true, std::memory_order_release);
			});
		}

		void DrawResults()
		{
			std::set<std::string> categories;
			for (const auto& entry : m_Entries)
				if (!entry.Category.empty())
					categories.insert(entry.Category);
			if (ImGui::BeginCombo("Category", m_Category.empty() ? "All" : m_Category.c_str()))
			{
				if (ImGui::Selectable("All", m_Category.empty()))
					m_Category.clear();
				for (const auto& category : categories)
					if (ImGui::Selectable(category.c_str(), category == m_Category))
						m_Category = category;
				ImGui::EndCombo();
			}
			const std::string query = Lower(m_Query.data());
			std::vector<const TextureLibrary::Entry*> filtered;
			for (const auto& entry : m_Entries)
			{
				if (!m_Category.empty() && entry.Category != m_Category)
					continue;
				if (query.empty() || m_Source == 1 || Lower(entry.Name + " " + entry.Category + " " + entry.Tags + " " + entry.Id).find(query) != std::string::npos)
					filtered.push_back(&entry);
			}
			const float tile = ImGui::GetFontSize() * 7.0f;
			const int columns = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / (tile + ImGui::GetStyle().CellPadding.x * 4)), 1, 8);
			if (ImGui::BeginChild("##TextureResults") && ImGui::BeginTable("##TextureGrid", columns, ImGuiTableFlags_SizingStretchSame))
			{
				ImGuiListClipper rows;
				rows.Begin((static_cast<int>(filtered.size()) + columns - 1) / columns);
				while (rows.Step())
				{
					for (int row = rows.DisplayStart; row < rows.DisplayEnd; ++row)
					{
						ImGui::TableNextRow();
						for (int col = 0; col < columns && row * columns + col < static_cast<int>(filtered.size()); ++col)
						{
							ImGui::TableSetColumnIndex(col);
							DrawEntry(*filtered[row * columns + col], tile);
						}
					}
				}
				ImGui::EndTable();
			}
			ImGui::EndChild();
		}

		void DrawEntry(const TextureLibrary::Entry& entry, float size)
		{
			const std::string key = entry.Source + "_" + entry.Id;
			ImGui::PushID(key.c_str());
			TextureLibrary::MaterialRequest request;
			std::copy(entry.Id.begin(), entry.Id.end(), request.Id.begin());
			request.AmbientCG = entry.Source == "ambientcg";
			request.Resolution = 1u << m_Resolution;
			const auto dragSource = [&request, &entry](ImGuiDragDropFlags flags) {
				if (ImGui::BeginDragDropSource(flags))
				{
					ImGui::SetDragDropPayload(TextureLibrary::PayloadType, &request, sizeof(request));
					ImGui::TextUnformatted(entry.Name.c_str());
					ImGui::EndDragDropSource();
				}
			};
			const auto thumbnail = m_Thumbnails.find(key);
			if (thumbnail == m_Thumbnails.end())
				StartPreview(entry, key);
			if (thumbnail != m_Thumbnails.end() && thumbnail->second && thumbnail->second->Loaded())
			{
				auto* renderer = Application::Get().GetImGuiLayer()->GetImGuiRenderer();
				ImGui::Image(renderer->CreateFrameTexture(thumbnail->second->GetImage()), ImVec2(size, size));
			}
			else
				ImGui::Dummy(ImVec2(size, size));
			dragSource(ImGuiDragDropFlags_SourceAllowNullID);
			const std::string link = (request.AmbientCG ? "https://ambientcg.com/a/" : "https://polyhaven.com/a/") + entry.Id;
			ImGui::TextLinkOpenURL(entry.Name.empty() ? entry.Id.c_str() : entry.Name.c_str(), link.c_str());
			if (ImGui::Button("Import"))
				StartDownload(request, m_OnLibraryImported);
			dragSource(ImGuiDragDropFlags_None);
			ImGui::PopID();
		}

	public:
		void SetLibraryImportCallback(OnImported callback) { m_OnLibraryImported = std::move(callback); }

	private:
		Ref<Project> m_Project;
		Ref<Job> m_Index, m_Download, m_Preview;
		OnImported m_OnImported, m_OnLibraryImported;
		std::vector<TextureLibrary::Entry> m_Entries;
		std::unordered_map<std::string, Ref<Texture2D>> m_Thumbnails;
		std::deque<std::string> m_ThumbnailOrder;
		std::string m_Status, m_Error, m_Category;
		std::array<char, 128> m_Query{};
		int m_Source = 0;
		int m_Resolution = 0;
		int m_ListSource = -1;
		int m_IndexSource = 0;
	};

}
