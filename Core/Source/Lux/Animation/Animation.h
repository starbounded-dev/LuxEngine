// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace Lux
{
	struct Vec3Key
	{
		float Time = 0.0f; // seconds
		glm::vec3 Value = glm::vec3(0.0f);
	};

	struct QuatKey
	{
		float Time = 0.0f; // seconds
		glm::quat Value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	};

	// Keys for one bone. An empty channel means "keep the skeleton's rest pose" for that channel.
	// Key times are ascending and lie in [0, Duration].
	struct BoneTrack
	{
		uint32_t BoneIndex = 0; // index into the skeleton the clip was imported against
		std::vector<Vec3Key> Translations;
		std::vector<QuatKey> Rotations;
		std::vector<Vec3Key> Scales;
	};

	// One clip: raw (uncompressed) keyframes per bone. Compression is A2's job; the on-disk format is
	// versioned so it can change then.
	class Animation : public Asset
	{
	public:
		static constexpr uint32_t MaxTracks = 4096;
		static constexpr uint32_t MaxKeysPerChannel = 1u << 20;

		static AssetType GetStaticType() { return AssetType::Animation; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		const std::string& GetName() const { return m_Name; }
		float GetDuration() const { return m_Duration; }
		AssetHandle GetSkeletonHandle() const { return m_SkeletonHandle; }
		const std::vector<BoneTrack>& GetTracks() const { return m_Tracks; }

		void SetName(std::string name) { m_Name = std::move(name); }
		void SetDuration(float seconds) { m_Duration = seconds; }
		void SetSkeletonHandle(AssetHandle handle) { m_SkeletonHandle = handle; }
		std::vector<BoneTrack>& GetTracksMutable() { return m_Tracks; }

		uint32_t GetNumKeys() const;

		// "LZAN" blob shared by the .lanim file and the asset pack. Deserialize returns false and leaves
		// the clip empty on a malformed stream (bad magic or version, counts above the limits, or
		// key times out of order).
		void Serialize(StreamWriter* writer) const;
		bool Deserialize(StreamReader* reader);

	private:
		void Clear();

		std::string m_Name;
		float m_Duration = 0.0f;
		AssetHandle m_SkeletonHandle = 0;
		std::vector<BoneTrack> m_Tracks;
	};
}
