// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Log.h"
#include "Lux/Core/Hash.h"
#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Utilities/FileSystem.h"

#include <stb/include/stb_image.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef LUX_PLATFORM_WINDOWS
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#elif defined(LUX_PLATFORM_LINUX)
#include <dlfcn.h>
#endif

namespace Lux {

	// Free CC0 textures from inside the editor: browse and search Poly Haven and ambientCG, download a texture's maps into
	// Assets/Textures/Library/<name>/ where the content browser picks them up. Both are public, key-less HTTP APIs; the panel is
	// optional (it does nothing until Refresh or Download is pressed). ZIP decompresses with the same stb library used for textures.
	namespace TextureLibrary {

		// ---- a minimal JSON reader (the two APIs return plain JSON; strings are decoded except for \u escapes) -----------------------
		struct Json
		{
			enum class Kind { Null, Bool, Number, String, Array, Object } Type = Kind::Null;
			double Number = 0.0;
			bool Bool = false;
			std::string String;
			std::string MemberName;
			std::vector<Json> Items; // arrays and object members retain their document order

			const Json* Find(const std::string& key) const
			{
				if (Type == Kind::Object)
					for (const auto& value : Items)
						if (value.MemberName == key)
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
			bool Parse(Json& out) { return ParseValue(out, 0) && (SkipSpace(), m_Pos == m_Text.size()); }

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
						value.MemberName = std::move(key);
						out.Items.push_back(std::move(value));
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
			constexpr size_t maxDownloadBytes = 128 * 1024 * 1024;
			if (!url.starts_with("https://"))
			{
				error = "expected an HTTPS download URL";
				return false;
			}
			{
				static std::mutex mutex;
				static std::chrono::steady_clock::time_point nextRequest;
				std::lock_guard lock(mutex);
				std::this_thread::sleep_until(nextRequest);
				nextRequest = std::chrono::steady_clock::now() + std::chrono::milliseconds(175);
			}
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
			if (session)
				WinHttpSetTimeouts(session, 10000, 10000, 10000, 20000);
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
				if (!WinHttpQueryDataAvailable(request, &available))
				{
					error = "the download was interrupted";
					ok = false;
					break;
				}
				if (available == 0)
					break;
				const size_t before = out.size();
				if (available > maxDownloadBytes - before)
				{
					error = "the download exceeds 128 MiB";
					ok = false;
					break;
				}
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
#elif defined(LUX_PLATFORM_LINUX)
			// libcurl.so.4's C ABI uses these stable option IDs. Loading only the required
			// symbols keeps catalog browsing available with the distro's runtime library.
			enum class CurlOption : int
			{
				WriteData = 10001, URL = 10002, WriteFunction = 20011, Timeout = 13, UserAgent = 10018,
				FollowLocation = 52, MaxRedirects = 68, ConnectTimeout = 78, NoSignal = 99,
				Protocols = 181, RedirectProtocols = 182
			};
			struct Curl
			{
				void* Library = dlopen("libcurl.so.4", RTLD_LOCAL | RTLD_NOW);
				int (*GlobalInit)(long) = Library ? reinterpret_cast<decltype(GlobalInit)>(dlsym(Library, "curl_global_init")) : nullptr;
				void* (*Init)() = Library ? reinterpret_cast<decltype(Init)>(dlsym(Library, "curl_easy_init")) : nullptr;
				int (*Set)(void*, CurlOption, ...) = Library ? reinterpret_cast<decltype(Set)>(dlsym(Library, "curl_easy_setopt")) : nullptr;
				int (*Perform)(void*) = Library ? reinterpret_cast<decltype(Perform)>(dlsym(Library, "curl_easy_perform")) : nullptr;
				int (*Info)(void*, int, ...) = Library ? reinterpret_cast<decltype(Info)>(dlsym(Library, "curl_easy_getinfo")) : nullptr;
				void (*Cleanup)(void*) = Library ? reinterpret_cast<decltype(Cleanup)>(dlsym(Library, "curl_easy_cleanup")) : nullptr;
				bool Ready = GlobalInit && Init && Set && Perform && Info && Cleanup && GlobalInit(3L) == 0;
			};
			static const Curl curl;
			void* handle = curl.Ready ? curl.Init() : nullptr;
			if (!handle)
			{
				error = "the system libcurl HTTPS client is unavailable";
				return false;
			}
			const auto write = +[](char* source, size_t elementSize, size_t count, void* user) -> size_t {
				auto& output = *static_cast<std::vector<uint8_t>*>(user);
				if (elementSize && count > (maxDownloadBytes - output.size()) / elementSize)
					return 0;
				const size_t bytes = elementSize * count;
				const auto* begin = reinterpret_cast<uint8_t*>(source);
				output.insert(output.end(), begin, begin + bytes);
				return bytes;
			};
			constexpr long https = 1L << 1;
			const auto set = [&](CurlOption option, auto value) { return curl.Set(handle, option, value) == 0; };
			const bool configured = set(CurlOption::URL, url.c_str()) && set(CurlOption::UserAgent, "LuxEngine") &&
				set(CurlOption::ConnectTimeout, 10L) && set(CurlOption::Timeout, 60L) && set(CurlOption::NoSignal, 1L) &&
				set(CurlOption::FollowLocation, 1L) && set(CurlOption::MaxRedirects, 4L) &&
				set(CurlOption::Protocols, https) && set(CurlOption::RedirectProtocols, https) &&
				set(CurlOption::WriteFunction, write) && set(CurlOption::WriteData, &out);
			const int result = configured ? curl.Perform(handle) : -1;
			long status = 0;
			curl.Info(handle, 0x200002 /* CURLINFO_RESPONSE_CODE */, &status);
			curl.Cleanup(handle);
			if (result == 0 && status == 200)
				return true;
			error = !configured ? "could not configure HTTPS in system libcurl"
				: status && status != 200 ? "HTTP " + std::to_string(status) : "libcurl error " + std::to_string(result);
			return false;
#else
			error = "an HTTPS client is unavailable in this build";
			return false;
#endif
		}

		inline bool ReadCache(const std::filesystem::path& path, std::vector<uint8_t>& bytes, size_t limit)
		{
			std::error_code ec;
			const auto size = std::filesystem::file_size(path, ec);
			if (ec || size == 0 || size > limit)
				return false;
			std::ifstream file(path, std::ios::binary);
			bytes.resize(static_cast<size_t>(size));
			return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())));
		}

		inline std::filesystem::path CacheDirectory()
		{
			return FileSystem::GetPersistentStoragePath() / "TextureLibrary";
		}

		inline bool HttpGetJson(const std::string& url, Json& out, std::string& error, bool cacheIndex = false)
		{
			std::vector<uint8_t> bytes;
			const std::filesystem::path cache = cacheIndex ? CacheDirectory() / (std::to_string(Hash::GenerateFNVHash(url)) + ".json") : std::filesystem::path{};
			const auto parse = [&out](const std::vector<uint8_t>& data) {
				const std::string text(data.begin(), data.end());
				Json parsed;
				if (!JsonParser(text).Parse(parsed))
					return false;
				out = std::move(parsed);
				return true;
			};
			std::error_code ec;
			const auto cacheTime = cacheIndex ? std::filesystem::last_write_time(cache, ec) : std::filesystem::file_time_type{};
			if (cacheIndex && !ec && std::filesystem::file_time_type::clock::now() - cacheTime < std::chrono::hours(12) &&
				ReadCache(cache, bytes, 8 * 1024 * 1024) && parse(bytes))
				return true;
			if (!HttpGet(url, bytes, error))
				return cacheIndex && ReadCache(cache, bytes, 8 * 1024 * 1024) && parse(bytes);
			if (!parse(bytes))
			{
				error = "the server returned invalid JSON";
				return false;
			}
			if (cacheIndex)
			{
				std::filesystem::create_directories(cache.parent_path(), ec);
				if (ec || !FileSystem::WriteFileAtomically(cache, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())))
					LUX_CORE_WARN_TAG("Editor", "[TextureLibrary] could not cache {}", cache.string());
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
			std::string Thumbnail;
			std::string Tags;
		};

		inline bool ValidId(std::string_view id)
		{
			return !id.empty() && id.size() <= 96 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
				return std::isalnum(c) || c == '_' || c == '-';
			});
		}

		inline std::string QueryEncode(std::string_view value)
		{
			constexpr char hex[] = "0123456789ABCDEF";
			std::string result;
			for (const unsigned char c : value)
			{
				if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
					result += static_cast<char>(c);
				else
				{
					result += '%';
					result += hex[c >> 4];
					result += hex[c & 15];
				}
			}
			return result;
		}

		inline bool ListPolyHaven(std::vector<Entry>& out, std::string& error)
		{
			Json root;
			if (!HttpGetJson("https://api.polyhaven.com/assets?t=textures", root, error, true))
				return false;
			out.clear();
			for (const auto& asset : root.Items)
			{
				const std::string& id = asset.MemberName;
				if (!ValidId(id))
					continue;
				Entry entry;
				entry.Id = id;
				entry.Name = asset.Text("name");
				if (const Json* categories = asset.Find("categories"); categories && !categories->Items.empty())
					entry.Category = categories->Items.front().String;
				entry.Source = "polyhaven";
				entry.Thumbnail = "https://cdn.polyhaven.com/asset_img/thumbs/" + id + ".png?width=160&height=160";
				if (const Json* tags = asset.Find("tags"))
					for (const Json& tag : tags->Items)
						entry.Tags += " " + tag.String;
				out.push_back(std::move(entry));
			}
			return true;
		}

		inline bool ListAmbientCG(const std::string& query, std::vector<Entry>& out, std::string& error)
		{
			const std::string url = "https://ambientcg.com/api/v2/full_json?type=Material&limit=100&sort=Popular&include=displayData,imageData,tagData";
			Json root;
			const bool maybeId = ValidId(query) && std::any_of(query.begin(), query.end(), [](unsigned char c) { return std::isdigit(c); });
			if (maybeId && !HttpGetJson(url + "&id=" + QueryEncode(query), root, error, true))
				return false;
			out.clear();
			const Json* found = root.Find("foundAssets");
			if (!found || found->Items.empty())
			{
				if (!HttpGetJson(query.empty() ? url : url + "&q=" + QueryEncode(query), root, error, true))
					return false;
				found = root.Find("foundAssets");
			}
			if (!found)
				return true;
			for (const Json& asset : found->Items)
			{
				Entry entry;
				entry.Id = asset.Text("assetId");
				if (!ValidId(entry.Id))
					continue;
				entry.Name = asset.Text("displayName").empty() ? entry.Id : asset.Text("displayName");
				entry.Category = asset.Text("displayCategory");
				entry.Source = "ambientcg";
				if (const Json* preview = asset.Find("previewImage"))
					entry.Thumbnail = preview->Text("256-PNG");
				if (const Json* tags = asset.Find("tags"))
					for (const Json& tag : tags->Items)
						entry.Tags += " " + tag.String;
				out.push_back(std::move(entry));
			}
			return true;
		}

		struct MaterialChannel
		{
			const char* FileSuffix;
			const char* PolyHavenKey;
			const char* AmbientCGSuffix;
		};

		inline constexpr std::array<MaterialChannel, 6> Channels {{
			{ "diffuse", "Diffuse", "Color" },
			{ "normal", "nor_gl", "NormalGL" },
			{ "roughness", "Rough", "Roughness" },
			{ "ao", "AO", "AmbientOcclusion" },
			{ "height", "Displacement", "Displacement" },
			{ "metalness", "Metal", "Metalness" }
		}};

		inline bool SaveMap(const std::filesystem::path& folder, const std::string& name, const std::vector<uint8_t>& bytes,
			std::vector<std::string>& written, std::string& error)
		{
			int width = 0, height = 0, channels = 0;
			if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) ||
				width <= 0 || height <= 0 || width > 4096 || height > 4096 || !WriteFile(folder / name, bytes))
			{
				error = "could not decode or save " + name;
				return false;
			}
			written.push_back(name);
			return true;
		}

		inline bool DownloadPolyHaven(const std::string& id, const std::filesystem::path& folder, std::vector<std::string>& written,
			std::string& error, uint32_t resolution = 1)
		{
			if (!ValidId(id) || (resolution != 1 && resolution != 2 && resolution != 4))
			{
				error = "invalid texture or resolution";
				return false;
			}
			Json files;
			if (!HttpGetJson("https://api.polyhaven.com/files/" + id, files, error))
				return false;
			const std::string tier = std::to_string(resolution) + "k";
			for (const MaterialChannel& channel : Channels)
			{
				const Json* map = files.Find(channel.PolyHavenKey);
				const Json* sizes = map ? map->Find(tier) : nullptr;
				const Json* jpeg = sizes ? sizes->Find("jpg") : nullptr;
				const bool dataMap = channel.FileSuffix == std::string_view("height") || channel.FileSuffix == std::string_view("metalness");
				const Json* png = sizes ? sizes->Find("png") : nullptr;
				const Json* format = png && (dataMap || !jpeg) ? png : jpeg;
				if (!format)
					continue;
				const std::string url = format->Text("url");
				std::vector<uint8_t> bytes;
				if (!HttpGet(url, bytes, error))
					return false;
				const std::string name = id + "_" + channel.FileSuffix + (format == png ? ".png" : ".jpg");
				if (!SaveMap(folder, name, bytes, written, error))
					return false;
			}
			if (written.empty())
				error = "no supported maps for " + id + " at " + tier;
			return !written.empty();
		}

		inline bool ReadMaterialZip(const std::vector<uint8_t>& zip, const std::string& id, const std::string& tier,
			const std::filesystem::path& folder, std::vector<std::string>& written, std::string& error)
		{
			const auto u16 = [&zip](size_t pos) -> uint32_t { return zip[pos] | (uint32_t(zip[pos + 1]) << 8u); };
			const auto u32 = [&u16](size_t pos) { return u16(pos) | (u16(pos + 2) << 16u); };
			error = "invalid material ZIP archive";
			if (zip.size() < 22)
				return false;
			const size_t lowerBound = zip.size() > 65557 ? zip.size() - 65557 : 0;
			size_t end = zip.size() - 22;
			while (u32(end) != 0x06054b50u || end + 22 + u16(end + 20) != zip.size())
			{
				if (end == lowerBound)
					return false;
				--end;
			}
			const uint32_t count = u16(end + 10);
			size_t central = u32(end + 16);
			if (u32(end + 4) != 0 || u16(end + 8) != count || count > 128 ||
				central > end || u32(end + 12) != end - central)
				return false;
			const std::string prefix = id + "_" + tier + "-JPG_";
			for (uint32_t i = 0; i < count; ++i)
			{
				if (end - central < 46 || u32(central) != 0x02014b50u)
					return false;
				const uint32_t nameLength = u16(central + 28);
				const uint32_t recordSize = 46 + nameLength + u16(central + 30) + u16(central + 32);
				if (recordSize > end - central)
					return false;
				const auto* nameBytes = reinterpret_cast<const char*>(zip.data() + central + 46);
				const std::string_view originalName(nameBytes, nameLength);
				const std::string_view suffix = originalName.starts_with(prefix) ? originalName.substr(prefix.size()) : std::string_view{};
				const size_t dot = suffix.rfind('.');
				const auto channel = std::find_if(Channels.begin(), Channels.end(), [suffix, dot](const MaterialChannel& map) {
					return suffix.substr(0, dot) == map.AmbientCGSuffix;
				});
				const uint32_t flags = u16(central + 8), method = u16(central + 10);
				const uint32_t packed = u32(central + 20), unpacked = u32(central + 24);
				const size_t local = u32(central + 42);
				central += recordSize;
				if (channel == Channels.end() || dot == std::string_view::npos)
					continue;
				const std::string_view ext = suffix.substr(dot);
				if (ext != ".jpg" && ext != ".png")
					continue;
				if ((flags & 1u) || (method != 0 && method != 8) || unpacked == 0 || unpacked > 64 * 1024 * 1024 ||
					local > end || end - local < 30 || u32(local) != 0x04034b50u)
					return false;
				const size_t offset = local + 30 + u16(local + 26) + u16(local + 28);
				if (offset > end || packed > end - offset || (method == 0 && packed != unpacked))
					return false;
				std::vector<uint8_t> bytes(unpacked);
				if (method == 0)
					std::memcpy(bytes.data(), zip.data() + offset, bytes.size());
				else if (stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()),
					reinterpret_cast<const char*>(zip.data() + offset), static_cast<int>(packed)) != static_cast<int>(bytes.size()))
					return false;
				const std::string name = id + "_" + channel->FileSuffix + std::string(ext);
				if (!SaveMap(folder, name, bytes, written, error))
					return false;
			}
			if (written.empty())
			{
				error = "archive contains no supported PBR maps";
				return false;
			}
			error.clear();
			return true;
		}

		inline bool DownloadAmbientCG(const std::string& id, const std::filesystem::path& folder, std::vector<std::string>& written,
			std::string& error, uint32_t resolution = 1)
		{
			if (!ValidId(id) || (resolution != 1 && resolution != 2 && resolution != 4))
			{
				error = "invalid texture or resolution";
				return false;
			}
			const std::string tier = std::to_string(resolution) + "K";
			Json root;
			if (!HttpGetJson("https://ambientcg.com/api/v2/full_json?type=Material&id=" + id + "&include=downloadData", root, error))
				return false;
			std::string link;
			if (const Json* found = root.Find("foundAssets"); found && !found->Items.empty())
			{
				const Json& asset = found->Items.front();
				if (const Json* folders = asset.Find("downloadFolders"))
					for (const auto& folderValue : folders->Items)
						if (const Json* categories = folderValue.Find("downloadFiletypeCategories"))
							for (const auto& type : categories->Items)
								if (const Json* downloads = type.Find("downloads"))
									for (const Json& download : downloads->Items)
										if (link.empty() && download.Text("attribute") == tier + "-JPG")
											link = download.Text("downloadLink");
			}
			if (link.empty())
			{
				error = "no " + tier + "-JPG download for " + id;
				return false;
			}
			std::vector<uint8_t> bytes;
			if (!HttpGet(link, bytes, error))
				return false;
			return ReadMaterialZip(bytes, id, tier, folder, written, error);
		}

		inline constexpr char PayloadType[] = "LUX_TEXTURE_LIBRARY";

		struct MaterialRequest
		{
			std::array<char, 97> Id{};
			uint32_t Resolution = 1;
			bool AmbientCG = false;
		};

		inline std::filesystem::path PackFolder(const MaterialRequest& request)
		{
			return std::filesystem::path("Textures/Library") / (std::string(request.AmbientCG ? "ambientcg_" : "polyhaven_") + request.Id.data())
				/ (std::to_string(request.Resolution) + "K");
		}

		inline std::filesystem::path FindMap(const std::filesystem::path& folder, const std::string& id, const MaterialChannel& channel)
		{
			std::error_code ec;
			for (const char* extension : { ".png", ".jpg" })
			{
				const auto path = folder / (id + "_" + channel.FileSuffix + extension);
				if (std::filesystem::is_regular_file(path, ec))
					return path;
			}
			return {};
		}

		inline bool FetchPack(const std::filesystem::path& assets, const MaterialRequest& request, std::string& error)
		{
			const std::string id = request.Id.data();
			if (!ValidId(id) || (request.Resolution != 1 && request.Resolution != 2 && request.Resolution != 4))
			{
				error = "invalid texture or resolution";
				return false;
			}
			const auto destination = assets / PackFolder(request);
			if (!FindMap(destination, id, Channels[0]).empty())
				return true;

			std::error_code ec;
			std::filesystem::create_directories(destination.parent_path(), ec);
			if (ec)
			{
				error = "could not create the material library folder";
				return false;
			}
			const auto staging = FileSystem::GetUniqueFileName(destination.parent_path() / ".download");
			if (!std::filesystem::create_directory(staging, ec))
			{
				error = "could not create a download staging folder";
				return false;
			}
			std::vector<std::string> maps;
			const bool downloaded = request.AmbientCG ? DownloadAmbientCG(id, staging, maps, error, request.Resolution)
				: DownloadPolyHaven(id, staging, maps, error, request.Resolution);
			if (downloaded && FindMap(staging, id, Channels[0]).empty())
				error = "the pack has no base colour image";
			else if (downloaded)
			{
				std::filesystem::rename(staging, destination, ec);
				if (!ec)
					return true;
				error = "could not move the downloaded material into the project";
			}
			for (const MaterialChannel& channel : Channels)
				for (const char* extension : { ".png", ".jpg" })
					std::filesystem::remove(staging / (id + "_" + channel.FileSuffix + extension), ec);
			std::filesystem::remove(staging, ec);
			return downloaded && !FindMap(destination, id, Channels[0]).empty();
		}

		// Main thread. MaterialAsset handles sRGB sampling for albedo. The data maps use their raw values.
		inline AssetHandle ImportPack(const MaterialRequest& request)
		{
			auto manager = Project::GetEditorAssetManager();
			if (!manager)
				return 0;
			const std::string id = request.Id.data();
			const auto folder = PackFolder(request);
			const auto materialPath = folder / (id + ".lmat");
			const auto assets = Project::GetActiveAssetDirectory();
			std::error_code ec;
			if (std::filesystem::is_regular_file(assets / materialPath, ec))
				return manager->GetOrImportAsset(materialPath);

			std::array<AssetHandle, Channels.size()> maps;
			for (size_t i = 0; i < Channels.size(); ++i)
			{
				const auto path = FindMap(assets / folder, id, Channels[i]);
				maps[i] = path.empty() ? AssetHandle(0) : manager->GetOrImportAsset(folder / path.filename());
				if (!path.empty() && !maps[i])
					return 0;
			}
			if (!maps[0])
				return 0;

			Ref<MaterialAsset> material = Ref<MaterialAsset>::Create();
			material->SetAlbedoColor(glm::vec3(1.0f));
			material->SetAlbedoMap(maps[0]);
			material->SetNormalMap(maps[1]);
			material->SetUseNormalMap(maps[1] != 0);
			material->SetRoughnessMap(maps[2]);
			material->SetRoughness(maps[2] ? 1.0f : 0.6f);
			material->SetMetalnessMap(maps[5]);
			material->SetMetalness(maps[5] ? 1.0f : 0.0f);
			MaterialSurfaceParameters surface = material->GetSurfaceParameters();
			surface.OcclusionMap = maps[3];
			surface.HeightMap = maps[4];
			surface.RoughnessChannel = MaterialTextureChannel::R;
			surface.MetalnessChannel = MaterialTextureChannel::R;
			material->SetSurfaceParameters(surface);
			AssetMetadata metadata;
			metadata.Type = AssetType::Material;
			metadata.FilePath = materialPath;
			AssetImporter::Serialize(metadata, material);
			return manager->GetOrImportAsset(materialPath);
		}

	}

}
