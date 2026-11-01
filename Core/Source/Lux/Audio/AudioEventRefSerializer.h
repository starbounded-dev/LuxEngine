// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AudioEventRef.h"

#include <string_view>

namespace Lux::Yaml { class Node; class Writer; }

// The one YAML form of AudioEventRef, shared by scenes, dialogue tables and surface tables. Both
// backends' references are written, so either build reopens the other's files intact. Sound is
// written only when set: a file with no native assignment is byte-for-byte what earlier builds
// wrote, and those builds ignore the key when it is present.
namespace Lux::AudioEventRefSerializer
{
	// key: { Guid, Path, BankName[, Sound] }
	void Write(Yaml::Writer& out, std::string_view key, const AudioEventRef& reference);
	// Reads the map Write produced. A missing node gives an empty reference; a missing key, an empty field.
	AudioEventRef Read(const Yaml::Node& node);

	// The flat keys AudioSourceComponent has always used: <prefix>Guid, <prefix>Path, <prefix>Bank
	// and <prefix>Sound, written into the map already open.
	void WriteFlat(Yaml::Writer& out, std::string_view prefix, const AudioEventRef& reference);
	AudioEventRef ReadFlat(const Yaml::Node& node, std::string_view prefix);
}
