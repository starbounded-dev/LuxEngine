// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioEventRef.h"

// What this backend plays: Studio events by GUID. A Sound asset handle is kept on the reference
// (scenes stay openable in both builds) but never played here.
namespace Lux
{
	AudioBackend AudioEngine::GetBackend()
	{
		return AudioBackend::FMOD;
	}

	bool AudioEventRef::IsValid() const
	{
		return !Guid.empty();
	}

	std::string AudioEventRef::GetReference() const
	{
		return Guid;
	}

	AudioEventRef AudioEventRef::FromReference(const std::string& reference)
	{
		AudioEventRef result;
		result.Guid = reference;
		return result;
	}
}
