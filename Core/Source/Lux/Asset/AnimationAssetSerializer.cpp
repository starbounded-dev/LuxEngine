// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationAssetSerializer.h"

#include "AssetManager.h"
#include "Lux/Animation/Animation.h"
#include "Lux/Animation/AnimationGraph.h"
#include "Lux/Animation/Skeleton.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/FileSystem.h"

#include <fstream>

namespace Lux
{
	namespace
	{
		template<typename T>
		bool SaveToFile(const AssetMetadata& metadata, const Ref<Asset>& asset)
		{
			const auto typed = asset.As<T>();
			if (!typed)
				return false;

			FileStreamWriter writer(Project::GetActiveAssetDirectory() / metadata.FilePath);
			typed->Serialize(&writer);
			if (!writer.IsStreamGood())
			{
				LUX_CORE_ERROR("Failed to save '{}'", metadata.FilePath.string());
				return false;
			}
			return true;
		}

		template<typename T>
		bool LoadFromFile(const AssetMetadata& metadata, Ref<Asset>& asset)
		{
			FileStreamReader reader(Project::GetActiveAssetDirectory() / metadata.FilePath);
			if (!reader.IsStreamGood())
			{
				LUX_CORE_ERROR("Cannot read '{}'", metadata.FilePath.string());
				return false;
			}
			auto typed = Ref<T>::Create();
			if (!typed->Deserialize(&reader))
			{
				LUX_CORE_ERROR("'{}' is not a valid {} file", metadata.FilePath.string(), AssetTypeToString(T::GetStaticType()));
				return false;
			}
			typed->Handle = metadata.Handle;
			asset = typed;
			return true;
		}

		template<typename T>
		bool WriteToPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo)
		{
			const auto typed = AssetManager::GetAsset<T>(handle);
			if (!typed)
				return false;
			outInfo.Offset = stream.GetStreamPosition();
			typed->Serialize(&stream);
			outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
			return stream.IsStreamGood();
		}

		template<typename T>
		Ref<Asset> ReadFromPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info)
		{
			stream.SetStreamPosition(info.PackedOffset);
			auto typed = Ref<T>::Create();
			if (!typed->Deserialize(&stream) || stream.GetStreamPosition() - info.PackedOffset != info.PackedSize)
			{
				LUX_CORE_ERROR("Invalid packed {}", AssetTypeToString(T::GetStaticType()));
				return nullptr;
			}
			return typed;
		}
	}

	namespace
	{
		constexpr uint64_t k_MaxGraphBytes = 4 * 1024 * 1024; // the loader, the saver and the pack share one limit
	}

	bool AnimationGraphSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<Asset> mutableAsset = asset; // Validate resolves indices, so it needs a non-const graph
		Ref<AnimationGraph> graph = mutableAsset.As<AnimationGraph>();
		std::string error;
		if (!graph || !graph->Validate(&error))
		{
			LUX_CORE_ERROR("Cannot save invalid animation graph '{}': {}", metadata.FilePath.string(), error);
			return false;
		}
		const std::string text = graph->ToYAML();
		if (text.size() > k_MaxGraphBytes)
		{
			LUX_CORE_ERROR("Animation graph '{}' exceeds the serialized size limit; not saved", metadata.FilePath.string());
			return false;
		}
		return FileSystem::WriteFileAtomically(Project::GetActiveAssetDirectory() / metadata.FilePath, text);
	}

	void AnimationGraphSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { TrySerialize(metadata, asset); }

	bool AnimationGraphSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		std::ifstream file(Project::GetActiveAssetDirectory() / metadata.FilePath, std::ios::binary | std::ios::ate);
		if (!file || file.tellg() < 0 || static_cast<uint64_t>(file.tellg()) > k_MaxGraphBytes)
		{
			LUX_CORE_ERROR("Cannot read animation graph '{}' (missing or oversized)", metadata.FilePath.string());
			return false;
		}
		std::string text(static_cast<size_t>(file.tellg()), '\0');
		file.seekg(0);
		if (!file.read(text.data(), text.size()))
			return false;
		auto graph = Ref<AnimationGraph>::Create();
		if (!graph->FromYAML(text))
			return false;
		graph->Handle = metadata.Handle;
		asset = graph;
		return true;
	}

	bool AnimationGraphSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		const auto graph = AssetManager::GetAsset<AnimationGraph>(handle);
		if (!graph)
			return false;
		const std::string text = graph->ToYAML();
		if (text.size() > k_MaxGraphBytes)
			return false;
		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteRaw<uint64_t>(text.size());
		stream.WriteData(text.data(), text.size());
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return stream.IsStreamGood();
	}

	Ref<Asset> AnimationGraphSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const
	{
		stream.SetStreamPosition(info.PackedOffset);
		uint64_t size = 0;
		if (info.PackedSize < sizeof(size) || !stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size))
			|| size > k_MaxGraphBytes || size != info.PackedSize - sizeof(size))
		{
			LUX_CORE_ERROR("Invalid packed animation graph size");
			return nullptr;
		}
		std::string text(static_cast<size_t>(size), '\0');
		if (!stream.ReadData(text.data(), text.size()))
			return nullptr;
		auto graph = Ref<AnimationGraph>::Create();
		return graph->FromYAML(text) ? graph : nullptr;
	}

	void SkeletonSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { TrySerialize(metadata, asset); }
	bool SkeletonSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { return SaveToFile<Skeleton>(metadata, asset); }
	bool SkeletonSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const { return LoadFromFile<Skeleton>(metadata, asset); }
	bool SkeletonSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const { return WriteToPack<Skeleton>(handle, stream, outInfo); }
	Ref<Asset> SkeletonSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const { return ReadFromPack<Skeleton>(stream, info); }

	void AnimationSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { TrySerialize(metadata, asset); }
	bool AnimationSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { return SaveToFile<Animation>(metadata, asset); }
	bool AnimationSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const { return LoadFromFile<Animation>(metadata, asset); }
	bool AnimationSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const { return WriteToPack<Animation>(handle, stream, outInfo); }
	Ref<Asset> AnimationSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const { return ReadFromPack<Animation>(stream, info); }
}
