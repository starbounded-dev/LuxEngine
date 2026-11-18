// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "RuntimeExportBranding.h"

#include "Lux/Core/Version.h"

#include "stb/include/stb_image.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#ifdef LUX_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Lux::RuntimeExport {

	namespace {

		template<typename T>
		void Append(std::vector<uint8_t>& out, const T& value)
		{
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			out.insert(out.end(), bytes, bytes + sizeof(T));
		}

		void AppendWide(std::vector<uint8_t>& out, const std::wstring& text, bool terminator = true)
		{
			for (wchar_t c : text)
				Append(out, static_cast<uint16_t>(c));
			if (terminator)
				Append(out, static_cast<uint16_t>(0));
		}

		void Align4(std::vector<uint8_t>& out)
		{
			while (out.size() % 4 != 0)
				out.push_back(0);
		}

		std::wstring ToWide(const std::string& utf8)
		{
			std::wstring result;
			for (size_t i = 0; i < utf8.size();)
			{
				const uint8_t lead = static_cast<uint8_t>(utf8[i]);
				uint32_t codePoint = lead;
				size_t length = 1;
				if (lead >= 0xF0 && i + 3 < utf8.size()) { codePoint = lead & 0x07; length = 4; }
				else if (lead >= 0xE0 && i + 2 < utf8.size()) { codePoint = lead & 0x0F; length = 3; }
				else if (lead >= 0xC0 && i + 1 < utf8.size()) { codePoint = lead & 0x1F; length = 2; }
				for (size_t k = 1; k < length; ++k)
					codePoint = (codePoint << 6) | (static_cast<uint8_t>(utf8[i + k]) & 0x3F);
				i += length;
				if (codePoint >= 0x10000)
				{
					codePoint -= 0x10000;
					result.push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
					result.push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
				}
				else
					result.push_back(static_cast<wchar_t>(codePoint));
			}
			return result;
		}

		// One node of the VS_VERSIONINFO tree: wLength, wValueLength, wType, key, padding, value, children.
		// A node's length covers its children, so children are built first.
		std::vector<uint8_t> VersionNode(const std::wstring& key, uint16_t type, const std::vector<uint8_t>& value, uint16_t valueLength,
			const std::vector<std::vector<uint8_t>>& children)
		{
			std::vector<uint8_t> node;
			Append(node, uint16_t(0)); // wLength, patched below
			Append(node, valueLength);
			Append(node, type);
			AppendWide(node, key);
			Align4(node);
			node.insert(node.end(), value.begin(), value.end());
			for (const auto& child : children)
			{
				Align4(node);
				node.insert(node.end(), child.begin(), child.end());
			}
			const uint16_t length = static_cast<uint16_t>(node.size());
			std::memcpy(node.data(), &length, sizeof(length));
			return node;
		}

		std::vector<uint8_t> VersionString(const std::wstring& key, const std::wstring& text)
		{
			std::vector<uint8_t> value;
			AppendWide(value, text);
			return VersionNode(key, 1, value, static_cast<uint16_t>(text.size() + 1), {});
		}

		// "1.2.3.4" -> four numbers, missing or non-numeric parts 0.
		std::array<uint16_t, 4> ParseVersion(const std::string& version)
		{
			std::array<uint16_t, 4> parts = {};
			size_t index = 0;
			uint32_t current = 0;
			bool digits = false;
			for (char c : version + ".")
			{
				if (c >= '0' && c <= '9')
				{
					current = std::min<uint32_t>(current * 10 + static_cast<uint32_t>(c - '0'), 65535);
					digits = true;
				}
				else
				{
					if (index < parts.size())
						parts[index] = static_cast<uint16_t>(digits ? current : 0);
					++index;
					current = 0;
					digits = false;
					if (c != '.')
						break;
				}
			}
			return parts;
		}

		// Box-filter the square canvas down to `size`; a canvas no larger than `size` is copied as is.
		std::vector<uint8_t> ScaleSquare(const std::vector<uint8_t>& canvas, uint32_t side, uint32_t size)
		{
			std::vector<uint8_t> out(static_cast<size_t>(size) * size * 4);
			for (uint32_t y = 0; y < size; ++y)
			{
				const uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(y) * side / size);
				const uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(y + 1) * side / size));
				for (uint32_t x = 0; x < size; ++x)
				{
					const uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(x) * side / size);
					const uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * side / size));
					// Colour is averaged weighted by alpha, so a transparent edge does not darken the icon.
					uint64_t r = 0, g = 0, b = 0, a = 0, count = 0;
					for (uint32_t sy = y0; sy < y1 && sy < side; ++sy)
						for (uint32_t sx = x0; sx < x1 && sx < side; ++sx)
						{
							const uint8_t* p = &canvas[(static_cast<size_t>(sy) * side + sx) * 4];
							r += static_cast<uint64_t>(p[0]) * p[3];
							g += static_cast<uint64_t>(p[1]) * p[3];
							b += static_cast<uint64_t>(p[2]) * p[3];
							a += p[3];
							++count;
						}
					uint8_t* o = &out[(static_cast<size_t>(y) * size + x) * 4];
					if (a)
					{
						o[0] = static_cast<uint8_t>(r / a);
						o[1] = static_cast<uint8_t>(g / a);
						o[2] = static_cast<uint8_t>(b / a);
					}
					o[3] = static_cast<uint8_t>(count ? a / count : 0);
				}
			}
			return out;
		}

		struct RgbaImage
		{
			std::vector<uint8_t> Pixels;
			uint32_t Width = 0, Height = 0;
		};

		[[maybe_unused]] bool LoadRgba(const std::filesystem::path& path, RgbaImage& out)
		{
			int width = 0, height = 0, channels = 0;
			stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
			if (!data)
				return false;
			out.Width = static_cast<uint32_t>(width);
			out.Height = static_cast<uint32_t>(height);
			out.Pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
			stbi_image_free(data);
			return out.Width > 0 && out.Height > 0;
		}

	}

	std::vector<IconImage> BuildIconImages(const uint8_t* rgba, uint32_t width, uint32_t height)
	{
		std::vector<IconImage> images;
		if (!rgba || !width || !height)
			return images;

		const uint32_t side = std::max(width, height);
		std::vector<uint8_t> canvas(static_cast<size_t>(side) * side * 4, 0);
		const uint32_t offsetX = (side - width) / 2, offsetY = (side - height) / 2;
		for (uint32_t y = 0; y < height; ++y)
			std::memcpy(&canvas[(static_cast<size_t>(y + offsetY) * side + offsetX) * 4], rgba + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4);

		// Windows asks for 16, 32 and 48 in the shell and 256 for large views; the others fill in the gaps.
		for (uint32_t size : { 256u, 128u, 64u, 48u, 32u, 16u })
		{
			if (size > side && size > 48)
				continue; // never upscale into the large sizes
			const std::vector<uint8_t> pixels = size == side ? canvas : ScaleSquare(canvas, side, size);

			IconImage image;
			image.Size = size;
			// BITMAPINFOHEADER: height covers the colour bitmap and the AND mask.
			Append(image.Resource, uint32_t(40));
			Append(image.Resource, int32_t(size));
			Append(image.Resource, int32_t(size * 2));
			Append(image.Resource, uint16_t(1));
			Append(image.Resource, uint16_t(32));
			Append(image.Resource, uint32_t(0)); // BI_RGB
			const uint32_t maskRow = ((size + 31) / 32) * 4;
			Append(image.Resource, uint32_t(size * size * 4 + maskRow * size));
			Append(image.Resource, int32_t(0));
			Append(image.Resource, int32_t(0));
			Append(image.Resource, uint32_t(0));
			Append(image.Resource, uint32_t(0));
			// Bottom-up BGRA.
			for (uint32_t y = size; y-- > 0;)
				for (uint32_t x = 0; x < size; ++x)
				{
					const uint8_t* p = &pixels[(static_cast<size_t>(y) * size + x) * 4];
					image.Resource.push_back(p[2]);
					image.Resource.push_back(p[1]);
					image.Resource.push_back(p[0]);
					image.Resource.push_back(p[3]);
				}
			// The alpha channel carries the transparency; the AND mask stays all zero.
			image.Resource.insert(image.Resource.end(), static_cast<size_t>(maskRow) * size, 0);
			images.push_back(std::move(image));
		}
		return images;
	}

	std::vector<uint8_t> BuildVersionResource(const std::string& gameName, const std::string& version, const std::string& originalFilename)
	{
		const auto parts = ParseVersion(version);
		const std::wstring name = ToWide(gameName), versionText = ToWide(version.empty() ? "1.0.0" : version);

		std::vector<uint8_t> fixedInfo;
		Append(fixedInfo, uint32_t(0xFEEF04BD)); // dwSignature
		Append(fixedInfo, uint32_t(0x00010000)); // dwStrucVersion
		Append(fixedInfo, uint32_t((uint32_t(parts[0]) << 16) | parts[1])); // file version MS
		Append(fixedInfo, uint32_t((uint32_t(parts[2]) << 16) | parts[3])); // file version LS
		Append(fixedInfo, uint32_t((uint32_t(parts[0]) << 16) | parts[1])); // product version MS
		Append(fixedInfo, uint32_t((uint32_t(parts[2]) << 16) | parts[3])); // product version LS
		Append(fixedInfo, uint32_t(0x3F)); // dwFileFlagsMask
		Append(fixedInfo, uint32_t(0)); // dwFileFlags
		Append(fixedInfo, uint32_t(0x00040004)); // VOS_NT_WINDOWS32
		Append(fixedInfo, uint32_t(1)); // VFT_APP
		Append(fixedInfo, uint32_t(0)); // dwFileSubtype
		Append(fixedInfo, uint32_t(0)); // dwFileDateMS
		Append(fixedInfo, uint32_t(0)); // dwFileDateLS

		const std::vector<std::vector<uint8_t>> strings = {
			VersionString(L"Comments", ToWide(std::format("Made with LuxEngine {}", LUX_VERSION))),
			VersionString(L"FileDescription", name),
			VersionString(L"FileVersion", versionText),
			VersionString(L"InternalName", name),
			VersionString(L"OriginalFilename", ToWide(originalFilename)),
			VersionString(L"ProductName", name),
			VersionString(L"ProductVersion", versionText),
		};
		const std::vector<uint8_t> stringTable = VersionNode(L"040904B0", 1, {}, 0, strings);
		const std::vector<uint8_t> stringFileInfo = VersionNode(L"StringFileInfo", 1, {}, 0, { stringTable });

		std::vector<uint8_t> translation;
		Append(translation, uint16_t(0x0409));
		Append(translation, uint16_t(0x04B0));
		const std::vector<uint8_t> varFileInfo = VersionNode(L"VarFileInfo", 1, {}, 0, { VersionNode(L"Translation", 0, translation, static_cast<uint16_t>(translation.size()), {}) });

		return VersionNode(L"VS_VERSION_INFO", 0, fixedInfo, static_cast<uint16_t>(fixedInfo.size()), { stringFileInfo, varFileInfo });
	}

#ifdef LUX_PLATFORM_WINDOWS

	namespace {

		BOOL CALLBACK CollectName(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param)
		{
			auto* names = reinterpret_cast<std::vector<std::wstring>*>(param);
			names->push_back(IS_INTRESOURCE(name) ? std::wstring(1, L'#') + std::to_wstring(reinterpret_cast<uintptr_t>(name)) : std::wstring(name));
			return TRUE;
		}

		// Names of every resource of a type in an executable, integer ids as "#<id>".
		std::vector<std::wstring> ListResources(const std::filesystem::path& file, LPCWSTR type)
		{
			std::vector<std::wstring> names;
			HMODULE module = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
			if (!module)
				return names;
			EnumResourceNamesW(module, type, CollectName, reinterpret_cast<LONG_PTR>(&names));
			FreeLibrary(module);
			return names;
		}

		LPCWSTR ResourceName(const std::wstring& name)
		{
			return name.size() > 1 && name[0] == L'#' ? MAKEINTRESOURCEW(std::stoul(name.substr(1))) : name.c_str();
		}

		BOOL CALLBACK CollectLanguage(HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR param)
		{
			reinterpret_cast<std::vector<WORD>*>(param)->push_back(language);
			return TRUE;
		}

		std::vector<WORD> ListLanguages(const std::filesystem::path& file, LPCWSTR type, const std::wstring& name)
		{
			std::vector<WORD> languages;
			HMODULE module = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
			if (!module)
				return languages;
			EnumResourceLanguagesW(module, type, ResourceName(name), CollectLanguage, reinterpret_cast<LONG_PTR>(&languages));
			FreeLibrary(module);
			return languages;
		}

		// Removes every resource of a type, whatever its name and language, so the replacement is the only one.
		void ClearResources(HANDLE update, const std::filesystem::path& file, LPCWSTR type)
		{
			for (const std::wstring& name : ListResources(file, type))
				for (WORD language : ListLanguages(file, type, name))
					UpdateResourceW(update, type, ResourceName(name), language, nullptr, 0);
		}

	}

	bool BrandExecutable(const std::filesystem::path& executable, const std::filesystem::path& iconSource,
		const std::string& gameName, const std::string& version, std::string& outWarning)
	{
		std::vector<IconImage> icons;
		if (!iconSource.empty())
		{
			RgbaImage image;
			if (LoadRgba(iconSource, image))
				icons = BuildIconImages(image.Pixels.data(), image.Width, image.Height);
			if (icons.empty())
				outWarning = std::format("The icon '{}' could not be read, so the executable keeps the default icon.", iconSource.string());
		}

		// The player's icon group is named GLFW_ICON (Lux-Runtime.rc), which is how GLFW finds the window
		// class icon the task bar shows. Replace it under the name it has.
		std::wstring groupName = L"GLFW_ICON";
		if (const auto groups = ListResources(executable, RT_GROUP_ICON); !groups.empty())
			groupName = groups.front();

		HANDLE update = BeginUpdateResourceW(executable.c_str(), FALSE);
		if (!update)
			return false;

		bool ok = true;
		constexpr WORD language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
		if (!icons.empty())
		{
			ClearResources(update, executable, RT_GROUP_ICON);
			ClearResources(update, executable, RT_ICON);

			// GRPICONDIR: reserved, type 1, count; then one GRPICONDIRENTRY per image, naming its RT_ICON id.
			std::vector<uint8_t> group;
			Append(group, uint16_t(0));
			Append(group, uint16_t(1));
			Append(group, static_cast<uint16_t>(icons.size()));
			for (size_t i = 0; i < icons.size(); ++i)
			{
				const uint16_t id = static_cast<uint16_t>(i + 1);
				ok &= UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(id), language,
					icons[i].Resource.data(), static_cast<DWORD>(icons[i].Resource.size())) != FALSE;
				group.push_back(icons[i].Size >= 256 ? 0 : static_cast<uint8_t>(icons[i].Size));
				group.push_back(icons[i].Size >= 256 ? 0 : static_cast<uint8_t>(icons[i].Size));
				group.push_back(0); // colour count
				group.push_back(0); // reserved
				Append(group, uint16_t(1)); // planes
				Append(group, uint16_t(32)); // bit count
				Append(group, static_cast<uint32_t>(icons[i].Resource.size()));
				Append(group, id);
			}
			ok &= UpdateResourceW(update, RT_GROUP_ICON, ResourceName(groupName), language, group.data(), static_cast<DWORD>(group.size())) != FALSE;
		}

		ClearResources(update, executable, RT_VERSION);
		const std::vector<uint8_t> versionResource = BuildVersionResource(gameName, version, executable.filename().string());
		ok &= UpdateResourceW(update, RT_VERSION, MAKEINTRESOURCEW(1), language, const_cast<uint8_t*>(versionResource.data()), static_cast<DWORD>(versionResource.size())) != FALSE;

		// Discard on failure: a half-written resource section would leave the player unable to start.
		if (!EndUpdateResourceW(update, !ok))
			return false;
		return ok;
	}

#else

	bool BrandExecutable(const std::filesystem::path&, const std::filesystem::path&, const std::string&, const std::string&, std::string&)
	{
		return true;
	}

#endif

}
