// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/UUID.h"

#include <string>

namespace Lux
{
	// What an audio source, zone, director or table entry plays. It holds one reference per backend,
	// so a scene saved by either build keeps the other's assignment:
	//   - FMOD: a Studio event GUID, with Path and BankName as advisory labels.
	//   - native: a Sound asset handle (.lsound).
	//
	// The GUID is the FMOD reference; the path is a cached label for the editor and is never used to
	// resolve the event. That split matters: renaming or moving an event in FMOD Studio changes its
	// path but not its GUID, so a path-referencing scene goes silently mute the first time a
	// designer reorganises the project - no error, no warning, just nothing plays.
	struct AudioEventRef
	{
		std::string Guid;   // "{xxxxxxxx-....}", empty when no event is assigned

		// Both advisory, both refreshed from the loaded banks on display, and neither ever used to
		// resolve the event - the GUID does that on its own, whichever bank the event turns out to
		// live in. Path is what a designer recognises; BankName lets the picker group by bank and
		// gives on-demand bank loading something to work from later.
		std::string Path;       // "event:/FX/Door"
		std::string BankName;   // "Master.bank"

		// The Sound asset the native backend plays; 0 when none is assigned. (An AssetHandle; spelled
		// as UUID so this header stays free of the asset module.)
		UUID Sound = 0;

		// Whether the backend this build compiled has something to play: a GUID for FMOD, a Sound
		// for native. Defined by the backend (Audio/FMOD or Audio/Native).
		bool IsValid() const;

		// Whether either backend's reference is set. Use this, not IsValid, where data is checked
		// independently of the build, such as validating an asset on load.
		bool HasReference() const { return !Guid.empty() || Sound != 0; }

		// The same assignment for both backends. Compares fields, so per-frame change checks
		// allocate nothing.
		bool SameAssignment(const AudioEventRef& other) const { return Guid == other.Guid && Sound == other.Sound; }

		// The string AudioEventInstance::Create resolves in this build, or empty when IsValid is
		// false: the GUID for FMOD, the Sound handle in decimal for native. Defined by the backend.
		std::string GetReference() const;

		// The inverse of GetReference: a reference holding only this backend's field. Defined by the
		// backend.
		static AudioEventRef FromReference(const std::string& reference);
	};

}
