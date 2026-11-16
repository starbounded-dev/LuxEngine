// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/UUID.h"

#include <string>

namespace Lux
{
	// What an audio source, zone, director or table entry plays: a Sound asset handle (.lsound), or for
	// a zone's snapshot, a mixer snapshot ID. Guid, Path and BankName are legacy FMOD fields: read from
	// older files so the editor and Play can report what still needs a Sound, never played.
	struct AudioEventRef
	{
		std::string Guid;       // legacy FMOD event GUID, "{xxxxxxxx-....}"
		std::string Path;       // legacy FMOD event path, "event:/FX/Door"
		std::string BankName;   // legacy FMOD bank name, "Master.bank"

		// The Sound asset it plays; 0 when none is assigned. (An AssetHandle; spelled as UUID so this
		// header stays free of the asset module.)
		UUID Sound = 0;

		// Whether it has something to play: a Sound. Defined in Audio/Native/NativeAudioReferences.cpp.
		bool IsValid() const;

		// Whether a Sound or a legacy FMOD GUID is set: an FMOD-only reference is what migration reports.
		bool HasReference() const { return !Guid.empty() || Sound != 0; }

		// The same assignment. Compares fields, so per-frame change checks allocate nothing.
		bool SameAssignment(const AudioEventRef& other) const { return Guid == other.Guid && Sound == other.Sound; }

		// The string AudioEventInstance::Create resolves (the Sound handle in decimal), or empty when
		// IsValid is false.
		std::string GetReference() const;

		// The inverse of GetReference: a reference holding only the Sound.
		static AudioEventRef FromReference(const std::string& reference);
	};

}
