// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// Saved references to assets. A reference in a project file is the asset's path relative to the
// project's Assets folder, so it survives a regenerated AssetRegistry.lzr, a fresh worktree and a
// clean clone (docs/ASSET_PATH_REFERENCES_PLAN.md, issue 161). Handles stay the runtime identity.
//
// Reading accepts both forms: a bare number is a handle (every older file), anything else is a path.
// Writing emits a path only inside a PathModeScope (a scene saved to its file); snapshots, undo
// diffs and asset packs stay numeric because the player has no path map.
//
// Thread: lookups take only the registry's shared lock and never import, so they are safe on the
// asset worker (Threading.md: the worker never mutates the registry). Editor only: the player has no
// EditorAssetManager, so a path read there resolves to a hashed handle; packs stay numeric.

#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Core/Log.h"
#include "Lux/Project/Project.h"
#include "Lux/Serialization/Yaml.h"

#include <charconv>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Lux::AssetReference
{
	inline bool& PathModeFlag()
	{
		thread_local bool s_PathMode = false;
		return s_PathMode;
	}

	// While alive on this thread, WriteValue emits paths for file-backed assets.
	struct PathModeScope
	{
		PathModeScope() : m_Previous(PathModeFlag()) { PathModeFlag() = true; }
		~PathModeScope() { PathModeFlag() = m_Previous; }
		PathModeScope(const PathModeScope&) = delete;
		PathModeScope& operator=(const PathModeScope&) = delete;

	private:
		bool m_Previous;
	};

	// A path that names no registered asset (file moved, not imported yet, or a runtime with no path
	// map) still gets a stable handle, hashed from the path, and is remembered here so saving writes
	// the same path back instead of erasing the reference.
	struct UnresolvedStore
	{
		std::mutex Mutex;
		std::unordered_map<uint64_t, std::string> Paths;
	};

	inline UnresolvedStore& Unresolved()
	{
		static UnresolvedStore s_Store;
		return s_Store;
	}

	inline uint64_t HashPath(std::string_view path)
	{
		uint64_t hash = 14695981039346656037ull; // FNV-1a, 64 bit
		for (const char c : path)
		{
			hash ^= static_cast<uint8_t>(c);
			hash *= 1099511628211ull;
		}
		return hash ? hash : 1;
	}

	// The whole string must be digits: "1234.gltf" is a path.
	inline bool TryParseHandle(std::string_view text, uint64_t& handle)
	{
		if (text.empty())
			return false;
		const char* begin = text.data();
		const char* end = begin + text.size();
		uint64_t value = 0;
		const auto [ptr, error] = std::from_chars(begin, end, value);
		if (error != std::errc() || ptr != end)
			return false;
		handle = value;
		return true;
	}

	inline Ref<EditorAssetManager> Manager()
	{
		// Ref::As is unchecked, so confirm the type: the player's manager is a RuntimeAssetManager.
		Ref<AssetManagerBase> base = Project::GetAssetManager();
		if (!base || !dynamic_cast<EditorAssetManager*>(base.Raw()))
			return {};
		return base.As<EditorAssetManager>();
	}

	inline AssetHandle ResolvePath(const std::string& path)
	{
		Ref<EditorAssetManager> manager = Manager();
		if (manager)
		{
			// A path this engine wrote is already a registry key: no parsing.
			if (const AssetHandle handle = manager->GetAssetHandleFromPathKey(path))
				return handle;
		}

		const std::filesystem::path relative = std::filesystem::path(path).lexically_normal();
		if (manager)
		{
			if (const AssetHandle handle = manager->GetAssetHandleFromFilePath(relative))
				return handle;
		}

		const std::string normalized = relative.generic_string();
		const uint64_t hashed = HashPath(normalized);
		UnresolvedStore& store = Unresolved();
		std::scoped_lock lock(store.Mutex);
		if (store.Paths.emplace(hashed, normalized).second)
			LUX_CORE_WARN("Asset reference '{}' does not resolve to a registered asset; the path is kept.", normalized);
		return AssetHandle(hashed);
	}

	// The path to save for a handle, or empty when it should be saved as a number.
	inline std::string PathFor(AssetHandle handle)
	{
		if (!handle)
			return {};

		std::string path;
		if (Ref<EditorAssetManager> manager = Manager())
		{
			const AssetMetadata metadata = manager->GetMetadata(handle);
			if (metadata.IsValid() && !metadata.FilePath.empty())
				path = metadata.FilePath.generic_string();
		}

		if (path.empty())
		{
			UnresolvedStore& store = Unresolved();
			std::scoped_lock lock(store.Mutex);
			if (const auto it = store.Paths.find(static_cast<uint64_t>(handle)); it != store.Paths.end())
				path = it->second;
		}

		uint64_t asNumber = 0;
		return TryParseHandle(path, asNumber) ? std::string() : path;
	}

	// Writes the value only: callers emit the key.
	inline void WriteValue(Yaml::Writer& out, AssetHandle handle)
	{
		if (PathModeFlag())
		{
			const std::string path = PathFor(handle);
			if (!path.empty())
			{
				out << path;
				return;
			}
		}
		out << handle;
	}

	inline AssetHandle ReadValue(const Yaml::Node& node, AssetHandle fallback = 0)
	{
		if (!node || !node.IsScalar())
			return fallback;

		const std::string text = node.as<std::string>("");
		if (text.empty())
			return fallback;

		uint64_t number = 0;
		if (TryParseHandle(text, number))
			return AssetHandle(number);
		return ResolvePath(text);
	}
}
