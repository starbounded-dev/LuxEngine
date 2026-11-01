// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Lux
{
	// How a 3D Sound gets quieter with distance d, clamped to [MinDistance, MaxDistance]:
	//   Inverse: MinDistance / (MinDistance + Rolloff * (d - MinDistance)); holds its last value past
	//            MaxDistance.
	//   Linear:  1 - Rolloff * (d - MinDistance) / (MaxDistance - MinDistance), floored at 0.
	//   None:    1.
	// The same formulas as miniaudio's attenuation models of those names.
	enum class SoundAttenuation : uint8_t { None, Inverse, Linear };

	struct SoundSpatialSettings
	{
		bool Enabled = true;   // 3D: panned and attenuated from the listener. False: 2D.
		SoundAttenuation Attenuation = SoundAttenuation::Inverse;
		float MinDistance = 1.0f;    // metres; full volume up to here
		float MaxDistance = 50.0f;   // metres; attenuation stops here, and distance culling uses it
		float Rolloff = 1.0f;
	};

	// What a Sound node does with its children when the Sound starts.
	enum class SoundNodeType : uint8_t
	{
		Clip,       // plays one audio clip; has no children
		Random,     // plays one child, chosen by weight
		Sequence,   // plays one child, the next in order each time the Sound starts (step sequence)
		Layer,      // plays every child together
		Switch      // plays the child at the index of a parameter's value (a label's index)
	};

	// What a parameter curve drives on its node, and the node's subtree.
	enum class SoundCurveTarget : uint8_t
	{
		Volume,     // linear gain multiplier, 0 to 4
		Pitch,      // playback rate multiplier, above 0 and up to 4
		LowPass,    // cutoff in Hz, 10 to 24000; the lowest on a path wins
		HighPass,   // cutoff in Hz, 10 to 24000; the highest on a path wins
		Send        // reverb send, 0 to 1
	};

	// What happens when a Sound at its instance limit starts again.
	enum class SoundStealPolicy : uint8_t
	{
		Oldest,     // stop the instance that started first
		Quietest,   // stop the instance that is currently quietest
		None        // refuse the new start
	};

	// A parameter that game code sets on an instance (AudioEventInstance::SetParameter). Continuous
	// between Minimum and Maximum, or labelled: then its values are the label indices 0..n-1, and
	// SetParameterLabel selects one by name.
	struct SoundParameter
	{
		std::string Name;
		float Minimum = 0.0f;
		float Maximum = 1.0f;
		float Default = 0.0f;
		std::vector<std::string> Labels;

		bool IsLabeled() const { return !Labels.empty(); }
	};

	// A piecewise-linear map from a parameter's value to a target value, held flat beyond its first
	// and last points. Points are ordered by strictly increasing parameter value.
	struct SoundCurve
	{
		std::string Parameter;
		SoundCurveTarget Target = SoundCurveTarget::Volume;
		std::vector<glm::vec2> Points;   // (parameter value, target value)

		float Evaluate(float value) const;
	};

	struct SoundNode
	{
		SoundNodeType Type = SoundNodeType::Clip;
		std::string Name;
		std::vector<uint32_t> Children;   // indices into Sound::Nodes, in order

		// Clip
		AssetHandle Clip = 0;     // an Audio asset (.wav, .ogg, .mp3, .flac)
		float StartOffset = 0.0f; // seconds into the clip where playback starts
		bool Loop = false;        // repeat forever: the whole clip, or the loop region when it has one
		bool HasLoopRegion = false;
		float LoopStart = 0.0f, LoopEnd = 0.0f;   // seconds; LoopStart < LoopEnd

		// Chance of being chosen by a Random parent, relative to its siblings.
		float Weight = 1.0f;
		// Random: never choose the child chosen last time, when there is another.
		bool AvoidRepeat = false;
		// Switch: the parameter whose value picks the child.
		std::string SwitchParameter;

		// Every node: multiplied down the tree to each clip it plays.
		float Volume = 1.0f;            // linear, 0 to 4
		float Pitch = 1.0f;             // above 0 and up to 4
		float VolumeVariation = 0.0f;   // dB, 0 to 24: each start lowers the volume by up to this much
		float PitchVariation = 0.0f;    // semitones, 0 to 12: each start shifts the pitch by up to this much either way
		std::vector<SoundCurve> Curves;
	};

	// A sound authored in the editor and played by the native audio backend (.lsound): a tree of
	// nodes (Nodes[0] is the root) resolved to the clips to play each time an instance starts, plus
	// the parameters game code drives. Instances read the asset when they start, so an edit applies to
	// the next start.
	class Sound : public Asset
	{
	public:
		static AssetType GetStaticType() { return AssetType::Sound; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		static constexpr uint32_t MaxNodes = 1024;
		static constexpr uint32_t MaxParameters = 32;
		static constexpr uint32_t MaxInstanceLimit = 256;
		static constexpr uint32_t MaxDepth = 64;   // nodes on the path from the root to any node

		std::vector<SoundNode> Nodes{ SoundNode{} };   // one empty clip until authored
		std::vector<SoundParameter> Parameters;

		std::string Bus = "bus:/";   // where it mixes; every bus plays through the master until the mixer exists
		float Volume = 1.0f;         // linear gain, 0 to 4
		float Pitch = 1.0f;          // playback rate, above 0 and up to 4
		SoundSpatialSettings Spatial;

		uint32_t InstanceLimit = 0;  // simultaneous instances; 0 is unlimited
		SoundStealPolicy Steal = SoundStealPolicy::Oldest;
		float Cooldown = 0.0f;       // seconds, 0 to 60: a start sooner than this after the last one is refused
		bool Stream = false;         // read from disk while playing instead of decoding into memory first

		// A Sound playing one clip, the form "Create Sound" gives an audio file.
		static Ref<Sound> CreateForClip(AssetHandle clip);

		// Continuous when any clip it can play loops; a one-shot otherwise.
		bool Loops() const;
		// Every clip handle in the tree, once each, in node order.
		std::vector<AssetHandle> GetClips() const;
		const SoundParameter* FindParameter(const std::string& name) const;

		// Writes the first problem to error, when given. An unassigned clip or an empty container is
		// valid while authoring; playing it reports the problem.
		bool Validate(std::string* error = nullptr) const;
		std::string ToYAML() const;
		// Replaces this sound's settings, or leaves them unchanged and reports why it failed. Reads
		// version 2 and the single-clip version 1.
		bool FromYAML(const std::string& text);
	};

	const char* SoundAttenuationName(SoundAttenuation attenuation);
	bool ParseSoundAttenuation(const std::string& name, SoundAttenuation& outAttenuation);
	const char* SoundNodeTypeName(SoundNodeType type);
	bool ParseSoundNodeType(const std::string& name, SoundNodeType& outType);
	const char* SoundCurveTargetName(SoundCurveTarget target);
	bool ParseSoundCurveTarget(const std::string& name, SoundCurveTarget& outTarget);
	const char* SoundStealPolicyName(SoundStealPolicy policy);
	bool ParseSoundStealPolicy(const std::string& name, SoundStealPolicy& outPolicy);
}
