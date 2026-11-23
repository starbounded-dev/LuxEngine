// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lightmap.h"

#include "Lux/Project/Project.h"

#include <fstream>

namespace Lux {

	namespace {

		constexpr char k_Magic[4] = { 'L', 'X', 'L', 'M' };
		constexpr uint32_t k_Version = 1;

		struct FileHeader
		{
			char Magic[4];
			uint32_t Version;
			uint32_t Width;
			uint32_t Height;
			uint32_t EntryCount;
		};

		struct FileEntry
		{
			uint64_t EntityID;
			uint32_t Submesh;
			float Rect[4];
		};

	}

	std::vector<uint32_t> LightmapData::PackedForGPU() const
	{
		std::vector<uint32_t> packed(static_cast<size_t>(Width) * Height * 2, 0u);
		for (size_t texel = 0; texel < static_cast<size_t>(Width) * Height; texel++)
		{
			packed[texel * 2 + 0] = static_cast<uint32_t>(Texels[texel * 4 + 0]) | (static_cast<uint32_t>(Texels[texel * 4 + 1]) << 16);
			packed[texel * 2 + 1] = static_cast<uint32_t>(Texels[texel * 4 + 2]) | (static_cast<uint32_t>(Texels[texel * 4 + 3]) << 16);
		}
		return packed;
	}

	bool LightmapData::Save(const std::filesystem::path& path) const
	{
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
			return false;

		FileHeader header = {};
		std::copy(std::begin(k_Magic), std::end(k_Magic), header.Magic);
		header.Version = k_Version;
		header.Width = Width;
		header.Height = Height;
		header.EntryCount = static_cast<uint32_t>(Entries.size());
		file.write(reinterpret_cast<const char*>(&header), sizeof(header));
		for (const Entry& entry : Entries)
		{
			FileEntry out = { entry.EntityID, entry.Submesh, { entry.Rect.x, entry.Rect.y, entry.Rect.z, entry.Rect.w } };
			file.write(reinterpret_cast<const char*>(&out), sizeof(out));
		}
		file.write(reinterpret_cast<const char*>(Texels.data()), static_cast<std::streamsize>(Texels.size() * sizeof(uint16_t)));
		return static_cast<bool>(file);
	}

	Ref<LightmapData> LightmapData::Load(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return nullptr;

		FileHeader header = {};
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!file || std::memcmp(header.Magic, k_Magic, 4) != 0 || header.Version != k_Version || header.Width == 0 || header.Height == 0
			|| header.Width > 8192 || header.Height > 8192 || header.EntryCount > 1000000)
			return nullptr;

		Ref<LightmapData> result = Ref<LightmapData>::Create();
		result->Width = header.Width;
		result->Height = header.Height;
		result->Entries.reserve(header.EntryCount);
		for (uint32_t i = 0; i < header.EntryCount; i++)
		{
			FileEntry in = {};
			file.read(reinterpret_cast<char*>(&in), sizeof(in));
			result->Entries.push_back({ in.EntityID, in.Submesh, glm::vec4(in.Rect[0], in.Rect[1], in.Rect[2], in.Rect[3]) });
		}
		result->Texels.resize(static_cast<size_t>(header.Width) * header.Height * 4);
		file.read(reinterpret_cast<char*>(result->Texels.data()), static_cast<std::streamsize>(result->Texels.size() * sizeof(uint16_t)));
		if (!file)
			return nullptr;
		return result;
	}

	std::filesystem::path LightmapData::PathForScene(const std::string& sceneName)
	{
		Ref<Project> project = Project::GetActive();
		if (!project || sceneName.empty())
			return {};
		return project->GetAssetDirectory() / "Lightmaps" / (sceneName + ".lxlm");
	}

}
