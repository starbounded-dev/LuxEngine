// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Animation.h"

namespace Lux
{
	namespace
	{
		constexpr char k_Magic[4] = { 'L', 'Z', 'A', 'N' };
		constexpr uint32_t k_Version = 1;

		template<typename Key>
		void WriteKeys(StreamWriter* writer, const std::vector<Key>& keys)
		{
			writer->WriteRaw<uint32_t>(static_cast<uint32_t>(keys.size()));
			for (const Key& key : keys)
			{
				writer->WriteRaw<float>(key.Time);
				writer->WriteRaw(key.Value);
			}
		}

		// Reads keys with ascending times inside [0, duration]; false on anything else.
		template<typename Key>
		bool ReadKeys(StreamReader* reader, std::vector<Key>& keys, float duration)
		{
			uint32_t count = 0;
			if (!reader->ReadRaw(count) || count > Animation::MaxKeysPerChannel)
				return false;

			keys.resize(count);
			float previous = 0.0f;
			for (Key& key : keys)
			{
				if (!reader->ReadRaw(key.Time) || !reader->ReadRaw(key.Value))
					return false;
				if (!(key.Time >= previous) || key.Time > duration) // also rejects NaN
					return false;
				previous = key.Time;
			}
			return true;
		}
	}

	uint32_t Animation::GetNumKeys() const
	{
		uint32_t total = 0;
		for (const BoneTrack& track : m_Tracks)
			total += static_cast<uint32_t>(track.Translations.size() + track.Rotations.size() + track.Scales.size());
		return total;
	}

	void Animation::Clear()
	{
		m_Name.clear();
		m_Duration = 0.0f;
		m_SkeletonHandle = 0;
		m_Tracks.clear();
	}

	void Animation::Serialize(StreamWriter* writer) const
	{
		writer->WriteData(k_Magic, sizeof(k_Magic));
		writer->WriteRaw<uint32_t>(k_Version);
		writer->WriteString(m_Name);
		writer->WriteRaw<float>(m_Duration);
		writer->WriteRaw<uint64_t>(static_cast<uint64_t>(m_SkeletonHandle));
		writer->WriteRaw<uint32_t>(static_cast<uint32_t>(m_Tracks.size()));
		for (const BoneTrack& track : m_Tracks)
		{
			writer->WriteRaw<uint32_t>(track.BoneIndex);
			WriteKeys(writer, track.Translations);
			WriteKeys(writer, track.Rotations);
			WriteKeys(writer, track.Scales);
		}
	}

	bool Animation::Deserialize(StreamReader* reader)
	{
		Clear();

		char magic[4] = {};
		uint32_t version = 0;
		uint64_t skeleton = 0;
		uint32_t trackCount = 0;
		if (!reader->ReadData(magic, sizeof(magic)) || memcmp(magic, k_Magic, sizeof(magic)) != 0
			|| !reader->ReadRaw(version) || version != k_Version
			|| !reader->ReadString(m_Name) || !reader->ReadRaw(m_Duration) || !(m_Duration >= 0.0f)
			|| !reader->ReadRaw(skeleton) || !reader->ReadRaw(trackCount) || trackCount > MaxTracks)
		{
			Clear();
			return false;
		}
		m_SkeletonHandle = skeleton;

		m_Tracks.resize(trackCount);
		for (BoneTrack& track : m_Tracks)
		{
			if (!reader->ReadRaw(track.BoneIndex)
				|| !ReadKeys(reader, track.Translations, m_Duration)
				|| !ReadKeys(reader, track.Rotations, m_Duration)
				|| !ReadKeys(reader, track.Scales, m_Duration))
			{
				Clear();
				return false;
			}
		}
		return true;
	}
}
