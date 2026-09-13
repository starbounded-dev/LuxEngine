#pragma once

#include <string>

namespace Lux
{
	// A reference to an FMOD Studio event.
	//
	// The GUID is the reference; the path is a cached label for the editor and is never used to
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

		bool IsValid() const { return !Guid.empty(); }
	};

}
