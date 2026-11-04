// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AudioEventRef.h"

#include <string_view>

namespace Lux::Yaml { class Node; class Writer; }

// The one YAML form of AudioEventRef, shared by scenes, dialogue tables and surface tables. Each key
// is written only when set: Sound always matters, and the legacy FMOD fields stay only while an old
// file still carries them (Project Settings > Audio > Remove FMOD data clears them).
namespace Lux::AudioEventRefSerializer
{
	// key: { [Guid, Path, BankName,] [Sound] }; an empty reference is an empty map.
	void Write(Yaml::Writer& out, std::string_view key, const AudioEventRef& reference);
	// Reads the map Write produced. A missing node gives an empty reference; a missing key, an empty field.
	AudioEventRef Read(const Yaml::Node& node);

	// The flat keys AudioSourceComponent has always used: <prefix>Guid, <prefix>Path, <prefix>Bank
	// and <prefix>Sound, written into the map already open.
	void WriteFlat(Yaml::Writer& out, std::string_view prefix, const AudioEventRef& reference);
	AudioEventRef ReadFlat(const Yaml::Node& node, std::string_view prefix);
}
