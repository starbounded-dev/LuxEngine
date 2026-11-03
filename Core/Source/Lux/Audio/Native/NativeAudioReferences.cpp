// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioEventRef.h"
#include "NativeInternal.h"

#include <charconv>

// References are Sound assets by handle. An FMOD-era event GUID is kept on the reference, so old
// scenes still open and report it, but it is never played.
namespace Lux
{
	bool NativeInternal::ParseHandle(const std::string& reference, uint64_t& outHandle)
	{
		uint64_t handle = 0;
		const char* end = reference.data() + reference.size();
		const auto [last, error] = std::from_chars(reference.data(), end, handle);
		if (reference.empty() || error != std::errc{} || last != end || handle == 0)
			return false;
		outHandle = handle;
		return true;
	}

	bool AudioEventRef::IsValid() const
	{
		return Sound != 0;
	}

	std::string AudioEventRef::GetReference() const
	{
		return Sound != 0 ? std::to_string(static_cast<uint64_t>(Sound)) : std::string{};
	}

	AudioEventRef AudioEventRef::FromReference(const std::string& reference)
	{
		AudioEventRef result;
		uint64_t handle = 0;
		if (NativeInternal::ParseHandle(reference, handle))
			result.Sound = handle;
		return result;
	}
}
