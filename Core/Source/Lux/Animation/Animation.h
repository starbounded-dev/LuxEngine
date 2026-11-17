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

	// A named moment in a clip that gameplay can react to: a footstep, a hit frame, a sound to play. Delivered
	// to the entity's script (OnAnimationEvent) and, when `Sound` is set, played at the entity.
	struct AnimationEvent
	{
		float Time = 0.0f;       // seconds into the clip
		std::string Name;
		std::string Argument;    // free text the script receives with the name
		AssetHandle Sound = 0;   // a Sound asset to play at the entity's position, or 0
	};

	// One clip: keyframes per bone, held uncompressed in memory and compressed on disk (see Serialize).
	class Animation : public Asset
	{
	public:
		static constexpr uint32_t MaxTracks = 4096;
		static constexpr uint32_t MaxKeysPerChannel = 1u << 20;
		static constexpr uint32_t MaxEvents = 4096;

		static AssetType GetStaticType() { return AssetType::Animation; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		const std::string& GetName() const { return m_Name; }
		float GetDuration() const { return m_Duration; }
		AssetHandle GetSkeletonHandle() const { return m_SkeletonHandle; }
		const std::vector<BoneTrack>& GetTracks() const { return m_Tracks; }
		const std::vector<AnimationEvent>& GetEvents() const { return m_Events; } // ascending by time

		void SetName(std::string name) { m_Name = std::move(name); }
		void SetDuration(float seconds) { m_Duration = seconds; }
		void SetSkeletonHandle(AssetHandle handle) { m_SkeletonHandle = handle; }
		std::vector<BoneTrack>& GetTracksMutable() { return m_Tracks; }
		void SetEvents(std::vector<AnimationEvent> events); // sorts by time (stable)

		uint32_t GetNumKeys() const;

		// "LZAN" blob shared by the .lanim file and the asset pack. Deserialize returns false and leaves
		// the clip empty on a malformed stream (bad magic or version, counts above the limits, or
		// key times out of order).
		// Serialize writes the compressed form with events (version 3): keys that their neighbours reproduce
		// within about 0.05 degrees / 0.1 mm are dropped, the rest quantized to 16 bits. SerializeRaw writes
		// every key exactly and no events (version 1, for tools and tests). Deserialize reads versions 1, 2
		// (compressed, no events) and 3, so older files still load.
		void Serialize(StreamWriter* writer) const;
		void SerializeRaw(StreamWriter* writer) const;
		bool Deserialize(StreamReader* reader);

	private:
		void Clear();

		std::string m_Name;
		float m_Duration = 0.0f;
		AssetHandle m_SkeletonHandle = 0;
		std::vector<BoneTrack> m_Tracks;
		std::vector<AnimationEvent> m_Events;
	};
}
