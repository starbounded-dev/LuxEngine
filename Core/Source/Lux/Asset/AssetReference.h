// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// Saved references to assets. A reference in a project file is the asset's path relative to the
// project's Assets folder, so it survives a regenerated AssetRegistry.lzr, a fresh worktree and a
// clean clone (docs/ASSET_PATH_REFERENCES_PLAN.md, issue 161). Handles stay the runtime identity.
//
// Reading accepts both forms: a bare number is a handle (every older file), anything else is a path.
// Writing emits a path only inside a PathModeScope (a file save); snapshots, undo diffs and asset
// packs stay numeric because the player has no path map.
//
// This header depends on nothing but handles and YAML, so code that is built without the asset
// system (the native audio tests) can include it. The registry is reached through Hooks, which
// EditorAssetManager installs. With no hooks a path reads as a hashed handle that is remembered and
// written back unchanged, and a handle is written as a number.
//
// Thread: the hooks take only the registry's shared lock and never import, so they are safe on the
// asset worker (Threading.md: the worker never mutates the registry).

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Log.h"
#include "Lux/Serialization/Yaml.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Lux::AssetReference
{
	// What the asset system provides. Each looks up the active project's EditorAssetManager when it
	// is called, so a project switch needs nothing from this header.
	struct Hooks
	{
		// Exact lookup by a registry path key (a normalized generic path): O(1), no parsing.
		AssetHandle (*FindByKey)(const std::string& key) = nullptr;
		// Lookup that normalizes the path first.
		AssetHandle (*FindByPath)(const std::string& path) = nullptr;
		// The registry's path for a handle; false when the handle is not a registered file asset.
		bool (*PathOfHandle)(AssetHandle handle, std::string& outPath) = nullptr;
		// Every registered path whose filename equals `filename`.
		void (*FindByFilename)(const std::string& filename, std::vector<std::string>& outPaths) = nullptr;
	};

	inline Hooks& GetHooks()
	{
		static Hooks s_Hooks;
		return s_Hooks;
	}

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

	// A path that names no registered asset (file moved, not imported yet, or no asset system) still
	// gets a stable handle, hashed from the path, and is remembered here so saving writes the same
	// path back instead of erasing the reference.
	struct UnresolvedStore
	{
		std::mutex Mutex;
		std::unordered_map<uint64_t, std::string> Paths;
		std::unordered_map<uint64_t, uint32_t> Counts;
		std::unordered_set<uint64_t> Reported; // already summarized in the log
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

	inline AssetHandle ResolvePath(const std::string& path)
	{
		const Hooks& hooks = GetHooks();
		if (hooks.FindByKey)
		{
			// A path this engine wrote is already a registry key: no parsing.
			if (const AssetHandle handle = hooks.FindByKey(path))
				return handle;
		}
		if (hooks.FindByPath)
		{
			if (const AssetHandle handle = hooks.FindByPath(path))
				return handle;
		}

		const uint64_t hashed = HashPath(path);
		UnresolvedStore& store = Unresolved();
		std::scoped_lock lock(store.Mutex);
		if (store.Paths.emplace(hashed, path).second)
			LUX_CORE_WARN("Asset reference '{}' does not resolve to a registered asset; the path is kept.", path);
		++store.Counts[hashed];
		return AssetHandle(hashed);
	}

	// The path to save for a handle, or empty when it should be saved as a number.
	inline std::string PathFor(AssetHandle handle)
	{
		if (!handle)
			return {};

		std::string path;
		const Hooks& hooks = GetHooks();
		if (!hooks.PathOfHandle || !hooks.PathOfHandle(handle, path))
		{
			path.clear();
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

	// One reference that did not resolve, with the registered files that share its filename.
	struct UnresolvedEntry
	{
		std::string Path;
		uint32_t Count = 0;
		std::vector<std::string> Candidates; // one: the likely move; several: a tie to decide
	};

	// Every reference that did not resolve so far, with the registered files that share its filename.
	// Read-only: the stored path is what a save writes back, so nothing here is ever cleared.
	inline std::vector<UnresolvedEntry> Report()
	{
		std::vector<UnresolvedEntry> entries;
		{
			UnresolvedStore& store = Unresolved();
			std::scoped_lock lock(store.Mutex);
			for (const auto& [hash, path] : store.Paths)
			{
				const auto count = store.Counts.find(hash);
				entries.push_back({ path, count != store.Counts.end() ? count->second : 0, {} });
			}
		}

		const Hooks& hooks = GetHooks();
		for (UnresolvedEntry& entry : entries)
		{
			const size_t slash = entry.Path.find_last_of('/');
			const std::string filename = slash == std::string::npos ? entry.Path : entry.Path.substr(slash + 1);
			if (hooks.FindByFilename)
				hooks.FindByFilename(filename, entry.Candidates);
		}
		return entries;
	}

	// Logs each unresolved reference once, with its candidates: one is the likely move, several are a
	// tie for the user to decide (never picked automatically).
	inline void LogNewUnresolved()
	{
		std::vector<uint64_t> fresh;
		{
			UnresolvedStore& store = Unresolved();
			std::scoped_lock lock(store.Mutex);
			for (const auto& [hash, path] : store.Paths)
				if (store.Reported.insert(hash).second)
					fresh.push_back(hash);
		}
		if (fresh.empty())
			return;

		for (const UnresolvedEntry& entry : Report())
		{
			if (std::find(fresh.begin(), fresh.end(), HashPath(entry.Path)) == fresh.end())
				continue;
			if (entry.Candidates.empty())
				LUX_CORE_WARN("Unresolved asset reference '{}' ({} uses): no registered file has that name.", entry.Path, entry.Count);
			else if (entry.Candidates.size() == 1)
				LUX_CORE_WARN("Unresolved asset reference '{}' ({} uses): '{}' has the same name and is the likely move.", entry.Path, entry.Count, entry.Candidates.front());
			else
				LUX_CORE_WARN("Unresolved asset reference '{}' ({} uses): {} files share the name, choose one.", entry.Path, entry.Count, entry.Candidates.size());
		}
	}
}
