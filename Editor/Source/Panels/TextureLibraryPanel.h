// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Log.h"
#include "Lux/Editor/EditorPanel.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/Project.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef LUX_PLATFORM_WINDOWS
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace Lux {

	// Free CC0 textures from inside the editor: browse and search Poly Haven and ambientCG, download a texture's maps into
	// Assets/Textures/Library/<name>/ where the content browser picks them up. Both are public, key-less HTTP APIs; the panel is
	// optional (it does nothing until Refresh or Download is pressed), the editor needs no extra install (WinHTTP is part of Windows,
	// zip archives from ambientCG are unpacked by the tar.exe that ships with Windows) and a missing network only shows an error line.
	// Windows only for now; elsewhere every request reports "not available on this platform". Header-only on purpose: no new .cpp.
	namespace TextureLibrary {

		// ---- a minimal JSON reader (the two APIs return plain JSON; strings are decoded except for \u escapes) -----------------------
		struct Json
		{
			enum class Kind { Null, Bool, Number, String, Array, Object } Type = Kind::Null;
			double Number = 0.0;
			bool Bool = false;
			std::string String;
			std::vector<Json> Items;                            // array elements
			std::vector<std::pair<std::string, Json>> Members;  // object members, in order

			const Json* Find(const std::string& key) const
			{
				for (const auto& [name, value] : Members)
					if (name == key)
						return &value;
				return nullptr;
			}
			std::string Text(const std::string& key) const
			{
				const Json* value = Find(key);
				return value && value->Type == Kind::String ? value->String : std::string();
			}
		};

		class JsonParser
		{
		public:
			explicit JsonParser(const std::string& text) : m_Text(text) {}
			bool Parse(Json& out) { SkipSpace(); return ParseValue(out, 0) && (SkipSpace(), true); }

		private:
			void SkipSpace() { while (m_Pos < m_Text.size() && std::isspace(static_cast<unsigned char>(m_Text[m_Pos]))) m_Pos++; }
			bool Expect(char c) { SkipSpace(); if (m_Pos < m_Text.size() && m_Text[m_Pos] == c) { m_Pos++; return true; } return false; }
			bool ParseString(std::string& out)
			{
				if (!Expect('"'))
					return false;
				while (m_Pos < m_Text.size() && m_Text[m_Pos] != '"')
				{
					char c = m_Text[m_Pos++];
					if (c == '\\' && m_Pos < m_Text.size())
					{
						const char e = m_Text[m_Pos++];
						switch (e)
						{
						case 'n': out += '\n'; break;
						case 't': out += '\t'; break;
						case 'r': out += '\r'; break;
						case 'u': out += '?'; m_Pos = std::min(m_Pos + 4, m_Text.size()); break;
						default: out += e; break;
						}
					}
					else
						out += c;
				}
				return m_Pos < m_Text.size() && m_Text[m_Pos++] == '"';
			}
			bool ParseValue(Json& out, int depth)
			{
				if (depth > 64)
					return false;
				SkipSpace();
				if (m_Pos >= m_Text.size())
					return false;
				const char c = m_Text[m_Pos];
				if (c == '{')
				{
					m_Pos++;
					out.Type = Json::Kind::Object;
					SkipSpace();
					if (Expect('}'))
						return true;
					do
					{
						std::string key;
						Json value;
						if (!ParseString(key) || !Expect(':') || !ParseValue(value, depth + 1))
							return false;
						out.Members.emplace_back(std::move(key), std::move(value));
					} while (Expect(','));
					return Expect('}');
				}
				if (c == '[')
				{
					m_Pos++;
					out.Type = Json::Kind::Array;
					SkipSpace();
					if (Expect(']'))
						return true;
					do
					{
						Json value;
						if (!ParseValue(value, depth + 1))
							return false;
						out.Items.push_back(std::move(value));
					} while (Expect(','));
					return Expect(']');
				}
				if (c == '"')
				{
					out.Type = Json::Kind::String;
					return ParseString(out.String);
				}
				if (m_Text.compare(m_Pos, 4, "true") == 0) { out.Type = Json::Kind::Bool; out.Bool = true; m_Pos += 4; return true; }
				if (m_Text.compare(m_Pos, 5, "false") == 0) { out.Type = Json::Kind::Bool; m_Pos += 5; return true; }
				if (m_Text.compare(m_Pos, 4, "null") == 0) { m_Pos += 4; return true; }
				char* end = nullptr;
				out.Number = std::strtod(m_Text.c_str() + m_Pos, &end);
				if (end == m_Text.c_str() + m_Pos)
					return false;
				out.Type = Json::Kind::Number;
				m_Pos = static_cast<size_t>(end - m_Text.c_str());
				return true;
			}

			const std::string& m_Text;
			size_t m_Pos = 0;
		};

		// ---- HTTP GET (WinHTTP) ---------------------------------------------------------------------------------------------------
		inline bool HttpGet(const std::string& url, std::vector<uint8_t>& out, std::string& error)
		{
			out.clear();
#ifdef LUX_PLATFORM_WINDOWS
			const std::wstring wideUrl(url.begin(), url.end());
			URL_COMPONENTS parts = {};
			parts.dwStructSize = sizeof(parts);
			wchar_t host[256] = {}, path[2048] = {};
			parts.lpszHostName = host;
			parts.dwHostNameLength = 255;
			parts.lpszUrlPath = path;
			parts.dwUrlPathLength = 2047;
			if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
			{
				error = "bad URL";
				return false;
			}
			HINTERNET session = WinHttpOpen(L"LuxEngine", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
			HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
			bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
			if (ok)
			{
				DWORD status = 0, size = sizeof(status);
				WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
				if (status != 200)
				{
					error = "HTTP " + std::to_string(status);
					ok = false;
				}
			}
			else
				error = "could not reach the server";
			while (ok)
			{
				DWORD available = 0;
				if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
					break;
				const size_t before = out.size();
				out.resize(before + available);
				DWORD read = 0;
				if (!WinHttpReadData(request, out.data() + before, available, &read))
				{
					error = "the download was interrupted";
					ok = false;
					break;
				}
				out.resize(before + read);
			}
			if (request) WinHttpCloseHandle(request);
			if (connection) WinHttpCloseHandle(connection);
			if (session) WinHttpCloseHandle(session);
			return ok;
#else
			(void)url;
			error = "not available on this platform yet";
			return false;
#endif
		}

		inline bool HttpGetJson(const std::string& url, Json& out, std::string& error)
		{
			std::vector<uint8_t> bytes;
			if (!HttpGet(url, bytes, error))
				return false;
			const std::string text(bytes.begin(), bytes.end());
			if (!JsonParser(text).Parse(out))
			{
				error = "the answer was not valid JSON";
				return false;
			}
			return true;
		}

		inline bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
		{
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			return static_cast<bool>(file);
		}

		// ---- the two libraries ----------------------------------------------------------------------------------------------------
		struct Entry
		{
			std::string Id;
			std::string Name;
			std::string Category;
			std::string Source; // "polyhaven" or "ambientcg"
		};

		inline bool ListPolyHaven(std::vector<Entry>& out, std::string& error)
		{
			Json root;
			if (!HttpGetJson("https://api.polyhaven.com/assets?t=textures", root, error))
				return false;
			out.clear();
			for (const auto& [id, asset] : root.Members)
			{
				Entry entry;
				entry.Id = id;
				entry.Name = asset.Text("name");
				if (const Json* categories = asset.Find("categories"); categories && !categories->Items.empty())
					entry.Category = categories->Items.front().String;
				entry.Source = "polyhaven";
				out.push_back(std::move(entry));
			}
			return true;
		}

		inline bool ListAmbientCG(const std::string& query, std::vector<Entry>& out, std::string& error)
		{
			std::string url = "https://ambientcg.com/api/v2/full_json?type=Material&limit=100&sort=Popular";
			if (!query.empty())
				url += "&q=" + query;
			Json root;
			if (!HttpGetJson(url, root, error))
				return false;
			out.clear();
			const Json* found = root.Find("foundAssets");
			if (!found)
				return true;
			for (const Json& asset : found->Items)
			{
				Entry entry;
				entry.Id = asset.Text("assetId");
				entry.Name = asset.Text("displayName").empty() ? entry.Id : asset.Text("displayName");
				entry.Category = asset.Text("displayCategory");
				entry.Source = "ambientcg";
				out.push_back(std::move(entry));
			}
			return true;
		}

		// Downloads the 1K maps of a Poly Haven texture (diffuse, normal, roughness, ambient occlusion) as <id>_<map>.<ext> in `folder`.
		inline bool DownloadPolyHaven(const std::string& id, const std::filesystem::path& folder, std::vector<std::string>& written, std::string& error)
		{
			Json files;
			if (!HttpGetJson("https://api.polyhaven.com/files/" + id, files, error))
				return false;
			const std::pair<const char*, const char*> maps[] = { { "Diffuse", "diffuse" }, { "nor_gl", "normal" }, { "Rough", "roughness" }, { "AO", "ao" } };
			for (const auto& [key, label] : maps)
			{
				const Json* map = files.Find(key);
				const Json* resolution = map ? map->Find("1k") : nullptr;
				const Json* format = resolution ? (resolution->Find("jpg") ? resolution->Find("jpg") : resolution->Find("png")) : nullptr;
				if (!format)
					continue;
				const std::string url = format->Text("url");
				std::vector<uint8_t> bytes;
				if (url.empty() || !HttpGet(url, bytes, error))
					return false;
				const std::string extension = url.substr(url.find_last_of('.'));
				const std::filesystem::path path = folder / (id + "_" + label + extension);
				if (!WriteFile(path, bytes))
				{
					error = "could not write " + path.string();
					return false;
				}
				written.push_back(path.filename().string());
			}
			return !written.empty();
		}

		// Downloads the 1K-JPG zip of an ambientCG material and unpacks it with the system tar.
		inline bool DownloadAmbientCG(const std::string& id, const std::filesystem::path& folder, std::vector<std::string>& written, std::string& error)
		{
			Json root;
			if (!HttpGetJson("https://ambientcg.com/api/v2/full_json?type=Material&id=" + id + "&include=downloadData", root, error))
				return false;
			std::string link;
			if (const Json* found = root.Find("foundAssets"); found && !found->Items.empty())
			{
				const Json& asset = found->Items.front();
				if (const Json* folders = asset.Find("downloadFolders"))
					for (const auto& [folderName, folderValue] : folders->Members)
						if (const Json* categories = folderValue.Find("downloadFiletypeCategories"))
							for (const auto& [typeName, type] : categories->Members)
								if (const Json* downloads = type.Find("downloads"))
									for (const Json& download : downloads->Items)
										if (link.empty() && download.Text("attribute") == "1K-JPG")
											link = download.Text("downloadLink");
			}
			if (link.empty())
			{
				error = "no 1K-JPG download for " + id;
				return false;
			}
			std::vector<uint8_t> bytes;
			if (!HttpGet(link, bytes, error))
				return false;
			const std::filesystem::path zip = folder / (id + "_1K-JPG.zip");
			if (!WriteFile(zip, bytes))
			{
				error = "could not write " + zip.string();
				return false;
			}
			// The tar.exe of Windows (bsdtar, reads zip), not whatever tar is first on PATH (Git's GNU tar does not).
			const char* systemRoot = std::getenv("SystemRoot");
			const std::string tar = (std::filesystem::path(systemRoot ? systemRoot : "C:/Windows") / "System32" / "tar.exe").string();
			const std::string command = "\"" + tar + "\" -xf \"" + zip.string() + "\" -C \"" + folder.string() + "\"";
			const int result = std::system(("\"" + command + "\"").c_str());
			std::error_code ec;
			std::filesystem::remove(zip, ec);
			if (result != 0)
			{
				error = "could not unpack the archive (tar failed)";
				return false;
			}
			for (const auto& item : std::filesystem::directory_iterator(folder, ec))
				written.push_back(item.path().filename().string());
			return !written.empty();
		}

	}

	class TextureLibraryPanel : public EditorPanel
	{
	public:
		virtual void OnImGuiRender(bool& isOpen) override
		{
			if (!isOpen)
				return;
			if (!ImGui::Begin("Texture Library", &isOpen))
			{
				ImGui::End();
				return;
			}
			ImGui::TextDisabled("Free CC0 textures (Poly Haven, ambientCG). Downloads go to Assets/Textures/Library/<name>/.");
			static const char* sources[] = { "Poly Haven", "ambientCG" };
			ImGui::SetNextItemWidth(140.0f);
			ImGui::Combo("Source", &m_Source, sources, 2);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputTextWithHint("##TextureSearch", "Search", &m_QueryBuffer[0], m_QueryBuffer.size());
			ImGui::SameLine();
			if (ImGui::Button("Refresh") && !m_State->Busy)
				StartList();
			if (m_State->Busy)
			{
				ImGui::SameLine();
				ImGui::TextUnformatted("working...");
			}
			{
				std::lock_guard<std::mutex> lock(m_State->Mutex);
				if (!m_State->Error.empty())
					ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s", m_State->Error.c_str());
				else if (!m_State->Status.empty())
					ImGui::TextDisabled("%s", m_State->Status.c_str());
			}

			const std::string query = ToLower(m_QueryBuffer.c_str());
			if (ImGui::BeginChild("##TextureResults", ImVec2(0, 0), true))
			{
				std::lock_guard<std::mutex> lock(m_State->Mutex);
				for (const TextureLibrary::Entry& entry : m_State->Entries)
				{
					if (!query.empty() && ToLower(entry.Name + " " + entry.Category + " " + entry.Id).find(query) == std::string::npos)
						continue;
					ImGui::PushID(entry.Id.c_str());
					if (ImGui::Button("Download") && !m_State->Busy)
						StartDownload(entry);
					ImGui::SameLine();
					ImGui::Text("%s", entry.Name.c_str());
					if (!entry.Category.empty())
					{
						ImGui::SameLine();
						ImGui::TextDisabled("(%s)", entry.Category.c_str());
					}
					ImGui::PopID();
				}
			}
			ImGui::EndChild();
			ImGui::End();
		}

	private:
		struct State
		{
			std::mutex Mutex;
			std::vector<TextureLibrary::Entry> Entries;
			std::string Status;
			std::string Error;
			std::atomic<bool> Busy{ false };
		};

		static std::string ToLower(std::string text)
		{
			for (char& c : text)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return text;
		}

		void StartList()
		{
			m_State->Busy = true;
			const int source = m_Source;
			const std::string query = m_QueryBuffer.c_str();
			std::shared_ptr<State> state = m_State;
			std::thread([state, source, query]() {
				std::vector<TextureLibrary::Entry> entries;
				std::string error;
				const bool ok = source == 0 ? TextureLibrary::ListPolyHaven(entries, error) : TextureLibrary::ListAmbientCG(query, entries, error);
				std::lock_guard<std::mutex> lock(state->Mutex);
				if (ok)
				{
					state->Entries = std::move(entries);
					state->Error.clear();
					state->Status = std::to_string(state->Entries.size()) + " textures";
				}
				else
					state->Error = "Could not load the list: " + error;
				state->Busy = false;
			}).detach();
		}

		void StartDownload(const TextureLibrary::Entry& entry)
		{
			const std::filesystem::path assets = Project::GetActiveAssetDirectory();
			if (assets.empty())
			{
				std::lock_guard<std::mutex> lock(m_State->Mutex);
				m_State->Error = "Open a project first.";
				return;
			}
			const std::filesystem::path folder = assets / "Textures" / "Library" / entry.Id;
			m_State->Busy = true;
			std::shared_ptr<State> state = m_State;
			std::thread([state, entry, folder]() {
				std::vector<std::string> written;
				std::string error;
				std::error_code ec;
				std::filesystem::create_directories(folder, ec);
				const bool ok = entry.Source == "polyhaven" ? TextureLibrary::DownloadPolyHaven(entry.Id, folder, written, error) : TextureLibrary::DownloadAmbientCG(entry.Id, folder, written, error);
				std::lock_guard<std::mutex> lock(state->Mutex);
				if (ok)
				{
					state->Error.clear();
					state->Status = "Downloaded " + std::to_string(written.size()) + " file(s) to Assets/Textures/Library/" + entry.Id;
				}
				else
					state->Error = "Download failed: " + error;
				state->Busy = false;
			}).detach();
		}

		std::shared_ptr<State> m_State = std::make_shared<State>();
		int m_Source = 0;
		std::string m_QueryBuffer = std::string(128, '\0');
	};

}
