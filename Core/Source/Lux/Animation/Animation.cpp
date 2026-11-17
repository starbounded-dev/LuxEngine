// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Animation.h"

#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr char k_Magic[4] = { 'L', 'Z', 'A', 'N' };
		constexpr uint32_t k_VersionRaw = 1;        // every key as written by the importer
		constexpr uint32_t k_VersionCompressed = 2; // reduced and quantized keys

		// Compression tolerances: a reduced channel stays within these of the original at every original key.
		constexpr float k_RotationTolerance = 0.0009f;  // radians, about 0.05 degrees
		constexpr float k_TranslationTolerance = 1e-4f; // plus 0.1% of the channel's extent
		constexpr float k_ScaleTolerance = 1e-4f;

		// Indices of the keys to keep (first and last always) so that interpolating between kept keys
		// reproduces every dropped key within `tolerance` (Douglas-Peucker over time). `error(i, a, b)` is how
		// far key i is from the interpolation of kept keys a and b.
		template<typename Error>
		std::vector<uint32_t> ReduceKeys(uint32_t count, float tolerance, Error error)
		{
			std::vector<bool> keep(count, false);
			keep[0] = true;
			keep[count - 1] = true;
			std::vector<std::pair<uint32_t, uint32_t>> stack{ { 0, count - 1 } };
			while (!stack.empty())
			{
				const auto [a, b] = stack.back();
				stack.pop_back();
				float worst = tolerance;
				uint32_t worstIndex = 0;
				for (uint32_t i = a + 1; i < b; i++)
				{
					const float e = error(i, a, b);
					if (e > worst)
					{
						worst = e;
						worstIndex = i;
					}
				}
				if (worstIndex)
				{
					keep[worstIndex] = true;
					stack.push_back({ a, worstIndex });
					stack.push_back({ worstIndex, b });
				}
			}
			std::vector<uint32_t> kept;
			for (uint32_t i = 0; i < count; i++)
			{
				if (keep[i])
					kept.push_back(i);
			}
			return kept;
		}

		float Fraction(float t0, float t1, float t)
		{
			return t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
		}

		uint16_t Quantize(float value, float low, float high)
		{
			const float range = high - low;
			const float t = range > 0.0f ? (value - low) / range : 0.0f;
			return static_cast<uint16_t>(std::clamp(std::lround(t * 65535.0f), 0l, 65535l));
		}

		float Dequantize(uint16_t q, float low, float high)
		{
			return low + (high - low) * (static_cast<float>(q) / 65535.0f);
		}

		// Key times are 16 bits over the duration, which is finer than a frame up to this length; longer clips
		// store them as floats.
		constexpr float k_MaxQuantizedDuration = 30.0f;

		void WriteTime(StreamWriter* writer, float time, float duration)
		{
			if (duration > k_MaxQuantizedDuration)
				writer->WriteRaw<float>(time);
			else
				writer->WriteRaw<uint16_t>(Quantize(time, 0.0f, duration));
		}

		// Reads a time; ordering is checked on the decoded value so both widths share one rule.
		bool ReadTime(StreamReader* reader, float duration, float& previous, float& time)
		{
			if (duration > k_MaxQuantizedDuration)
			{
				if (!reader->ReadRaw(time))
					return false;
			}
			else
			{
				uint16_t q = 0;
				if (!reader->ReadRaw(q))
					return false;
				time = std::min(Dequantize(q, 0.0f, duration), duration);
			}
			if (!(time >= previous) || time > duration) // also rejects NaN
				return false;
			previous = time;
			return true;
		}

		// A vector channel: reduced, then 16 bits per component over the kept keys' bounds. One key is stored exactly.
		void WriteVec3Channel(StreamWriter* writer, const std::vector<Vec3Key>& keys, float duration, float baseTolerance)
		{
			writer->WriteRaw<uint32_t>(static_cast<uint32_t>(keys.size()));
			if (keys.empty())
				return;
			if (keys.size() == 1)
			{
				writer->WriteRaw<float>(keys[0].Time);
				writer->WriteRaw(keys[0].Value);
				return;
			}
			glm::vec3 low(1e30f), high(-1e30f);
			for (const Vec3Key& key : keys)
			{
				low = glm::min(low, key.Value);
				high = glm::max(high, key.Value);
			}
			// The extent term is capped: a root track that travels far must not get a coarse tolerance (foot sliding).
			const float tolerance = baseTolerance + std::min(1e-3f * glm::length(high - low), 1e-3f);
			const std::vector<uint32_t> kept = ReduceKeys(static_cast<uint32_t>(keys.size()), tolerance, [&](uint32_t i, uint32_t a, uint32_t b)
			{
				const float t = Fraction(keys[a].Time, keys[b].Time, keys[i].Time);
				return glm::length(glm::mix(keys[a].Value, keys[b].Value, t) - keys[i].Value);
			});
			glm::vec3 keptLow(1e30f), keptHigh(-1e30f);
			for (uint32_t i : kept)
			{
				keptLow = glm::min(keptLow, keys[i].Value);
				keptHigh = glm::max(keptHigh, keys[i].Value);
			}
			writer->WriteRaw<uint32_t>(static_cast<uint32_t>(kept.size()));
			writer->WriteRaw(keptLow);
			writer->WriteRaw(keptHigh);
			for (uint32_t i : kept)
			{
				WriteTime(writer, keys[i].Time, duration);
				for (int c = 0; c < 3; c++)
					writer->WriteRaw<uint16_t>(Quantize(keys[i].Value[c], keptLow[c], keptHigh[c]));
			}
		}

		bool ReadVec3Channel(StreamReader* reader, std::vector<Vec3Key>& keys, float duration)
		{
			uint32_t count = 0;
			if (!reader->ReadRaw(count) || count > Animation::MaxKeysPerChannel)
				return false;
			keys.resize(count);
			if (count == 0)
				return true;
			if (count == 1)
				return reader->ReadRaw(keys[0].Time) && reader->ReadRaw(keys[0].Value) && keys[0].Time >= 0.0f && keys[0].Time <= duration;
			uint32_t kept = 0;
			glm::vec3 low, high;
			if (!reader->ReadRaw(kept) || kept < 2 || kept > count || !reader->ReadRaw(low) || !reader->ReadRaw(high))
				return false;
			keys.resize(kept);
			float previous = 0.0f;
			for (Vec3Key& key : keys)
			{
				uint16_t q[3] = {};
				if (!ReadTime(reader, duration, previous, key.Time) || !reader->ReadRaw(q[0]) || !reader->ReadRaw(q[1]) || !reader->ReadRaw(q[2]))
					return false;
				for (int c = 0; c < 3; c++)
					key.Value[c] = Dequantize(q[c], low[c], high[c]);
			}
			return true;
		}

		constexpr float k_QuatRange = 0.70710678f; // the three smaller components of a unit quaternion lie in +-1/sqrt(2)

		// A rotation channel: reduced by angle, then stored smallest-three (the largest component is rebuilt).
		void WriteQuatChannel(StreamWriter* writer, const std::vector<QuatKey>& keys, float duration)
		{
			writer->WriteRaw<uint32_t>(static_cast<uint32_t>(keys.size()));
			if (keys.empty())
				return;
			if (keys.size() == 1)
			{
				writer->WriteRaw<float>(keys[0].Time);
				writer->WriteRaw(keys[0].Value);
				return;
			}
			const std::vector<uint32_t> kept = ReduceKeys(static_cast<uint32_t>(keys.size()), k_RotationTolerance, [&](uint32_t i, uint32_t a, uint32_t b)
			{
				const float t = Fraction(keys[a].Time, keys[b].Time, keys[i].Time);
				const glm::quat mixed = glm::normalize(glm::slerp(keys[a].Value, keys[b].Value, t));
				return 2.0f * std::acos(std::min(1.0f, std::abs(glm::dot(mixed, keys[i].Value))));
			});
			writer->WriteRaw<uint32_t>(static_cast<uint32_t>(kept.size()));
			for (uint32_t i : kept)
			{
				const glm::quat q = glm::normalize(keys[i].Value);
				const float c[4] = { q.x, q.y, q.z, q.w };
				int largest = 0;
				for (int k = 1; k < 4; k++)
				{
					if (std::abs(c[k]) > std::abs(c[largest]))
						largest = k;
				}
				const float sign = c[largest] < 0.0f ? -1.0f : 1.0f; // q and -q are the same rotation
				WriteTime(writer, keys[i].Time, duration);
				writer->WriteRaw<uint8_t>(static_cast<uint8_t>(largest));
				for (int k = 0; k < 4; k++)
				{
					if (k != largest)
						writer->WriteRaw<uint16_t>(Quantize(sign * c[k], -k_QuatRange, k_QuatRange));
				}
			}
		}

		bool ReadQuatChannel(StreamReader* reader, std::vector<QuatKey>& keys, float duration)
		{
			uint32_t count = 0;
			if (!reader->ReadRaw(count) || count > Animation::MaxKeysPerChannel)
				return false;
			keys.resize(count);
			if (count == 0)
				return true;
			if (count == 1)
				return reader->ReadRaw(keys[0].Time) && reader->ReadRaw(keys[0].Value) && keys[0].Time >= 0.0f && keys[0].Time <= duration;
			uint32_t kept = 0;
			if (!reader->ReadRaw(kept) || kept < 2 || kept > count)
				return false;
			keys.resize(kept);
			float previous = 0.0f;
			for (QuatKey& key : keys)
			{
				uint8_t largest = 0;
				if (!ReadTime(reader, duration, previous, key.Time) || !reader->ReadRaw(largest) || largest > 3)
					return false;
				float c[4] = {};
				float sum = 0.0f;
				for (int k = 0; k < 4; k++)
				{
					if (k == largest)
						continue;
					uint16_t q = 0;
					if (!reader->ReadRaw(q))
						return false;
					c[k] = Dequantize(q, -k_QuatRange, k_QuatRange);
					sum += c[k] * c[k];
				}
				c[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
				key.Value = glm::normalize(glm::quat(c[3], c[0], c[1], c[2]));
			}
			return true;
		}

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
		writer->WriteRaw<uint32_t>(k_VersionCompressed);
		writer->WriteString(m_Name);
		writer->WriteRaw<float>(m_Duration);
		writer->WriteRaw<uint64_t>(static_cast<uint64_t>(m_SkeletonHandle));
		writer->WriteRaw<uint32_t>(static_cast<uint32_t>(m_Tracks.size()));
		for (const BoneTrack& track : m_Tracks)
		{
			writer->WriteRaw<uint32_t>(track.BoneIndex);
			WriteVec3Channel(writer, track.Translations, m_Duration, k_TranslationTolerance);
			WriteQuatChannel(writer, track.Rotations, m_Duration);
			WriteVec3Channel(writer, track.Scales, m_Duration, k_ScaleTolerance);
		}
	}

	void Animation::SerializeRaw(StreamWriter* writer) const
	{
		writer->WriteData(k_Magic, sizeof(k_Magic));
		writer->WriteRaw<uint32_t>(k_VersionRaw);
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
			|| !reader->ReadRaw(version) || (version != k_VersionRaw && version != k_VersionCompressed)
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
			const bool compressed = version == k_VersionCompressed;
			if (!reader->ReadRaw(track.BoneIndex)
				|| !(compressed ? ReadVec3Channel(reader, track.Translations, m_Duration) : ReadKeys(reader, track.Translations, m_Duration))
				|| !(compressed ? ReadQuatChannel(reader, track.Rotations, m_Duration) : ReadKeys(reader, track.Rotations, m_Duration))
				|| !(compressed ? ReadVec3Channel(reader, track.Scales, m_Duration) : ReadKeys(reader, track.Scales, m_Duration)))
			{
				Clear();
				return false;
			}
		}
		return true;
	}
}
