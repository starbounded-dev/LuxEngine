// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AudioBankManifest.h"
#include "Lux/Core/UUID.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// No backend type appears here. Exactly one implementation is compiled, chosen by premake's
// --audio-backend: Audio/FMOD/ (default, FMODInternal.h is its private header) or Audio/Native/
// (miniaudio, NativeInternal.h).
namespace Lux {

	// The playback backend this build compiled. Editor code asks AudioEngine::GetBackend() instead of
	// testing a define: the backend defines are set for Core only.
	enum class AudioBackend { FMOD, Native };

	// Read-only backend playback statistics for editor tooling.
	struct AudioEngineStats
	{
		const char* BackendName = "None";
		bool Initialized = false;

		// Version of the backend itself, when it reports one (FMOD). Zero otherwise.
		uint32_t VersionMajor = 0;
		uint32_t VersionMinor = 0;
		uint32_t VersionPatch = 0;

		int SampleRate = 0;

		// False until mixer statistics are available from the initialized FMOD system.
		bool HasMixerStats = false;
		int ChannelsPlaying = 0;
		int RealChannelsPlaying = 0;
		float DSPCPUPercent = 0.0f;
		float StreamCPUPercent = 0.0f;
		float UpdateCPUPercent = 0.0f;
		int MemoryCurrentBytes = 0;
		int MemoryPeakBytes = 0;

		// FMOD Studio layer. Zeroed before initialization.
		bool LiveUpdateEnabled = false;
		int LoadedBankCount = 0;
		int EventDescriptionCount = 0;
		int PlayingEventInstances = 0;
	};

	// A bank loaded from the project's built bank directory. Banks are build output of the FMOD
	// Studio project - see AudioBankBuilder.
	struct AudioBankInfo
	{
		std::string Name;              // file name as it sits on disk, e.g. "Master.bank"
		int EventCount = 0;

		// The strings bank carries the event *path* table. Without it events can only be resolved
		// by GUID, so its absence is worth surfacing rather than discovering through a failed
		// lookup.
		bool IsStringsBank = false;
	};

	// One event the loaded banks describe. Enumerated from the banks themselves rather than from
	// the GUIDs.txt that fmodstudiocl exports, so the list can never disagree with what is actually
	// loaded.
	struct AudioEventInfo
	{
		std::string Path;        // "event:/FX/Door"
		std::string Guid;        // "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" - the stable reference
		std::string BankName;    // the bank that describes it, e.g. "Master.bank"
		bool Is3D = false;
		bool IsOneshot = false;
		bool IsSnapshot = false;
	};

	// A writable local parameter authored on an event (not read-only, automatic or global).
	struct AudioEventParameterInfo
	{
		std::string Name;    // the canonical authored name, which may differ in case from the lookup
		float Minimum = 0.0f;
		float Maximum = 0.0f;
		float Default = 0.0f;
		bool Discrete = false;
		bool Labeled = false;
	};

	class AudioEngine
	{
	public:
		static AudioBackend GetBackend();

		static void Init();
		static void Shutdown();

		// Queried per frame by the editor's Audio Debugger. Uses inexpensive mixer queries;
		// safe to call before Init or after Shutdown, where it reports Initialized = false.
		static AudioEngineStats GetStats();

		static void Update();
		// Main thread; output mute is independent of authored bus and gameplay pause state.
		static void SetApplicationFocused(bool focused);

		// --- Sound Banks (the native backend) ---------------------------------------------------
		//
		// A Sound Bank (.lbank) lists Sounds that load and unload together. Loading decodes the
		// clips of its non-streaming Sounds off the main thread and returns at once; loads are
		// counted, so a bank stays loaded until every load is matched by an unload; a Resident bank stays
		// loaded until the engine shuts down, whatever unloads follow. A clip stays in
		// memory while a loaded bank lists it or a voice plays it. False for an unknown bank. The
		// handles are Sound Bank asset handles. Main thread only. FMOD builds load nothing.
		static bool LoadSoundBank(UUID bank);
		static void UnloadSoundBank(UUID bank);
		// Loaded, and every clip it decodes is ready (or failed, and was reported).
		static bool IsSoundBankLoaded(UUID bank);
		// A Sound Bank by handle (in decimal) or by its path relative to Assets, with or without the
		// .lbank extension; 0, with the reason logged, when nothing matches.
		static UUID FindSoundBank(const std::string& reference);
		// Frees clips that no loaded bank lists and no voice plays: those Sounds outside any bank
		// loaded when they first played. Called when a scene stops.
		static void ReleaseUnusedClips();

		// --- FMOD Studio banks and events -------------------------------------------------------
		//
		// Banks are what the engine actually consumes: the .fspro is authored in the FMOD Studio
		// app and AudioBankBuilder turns it into these. Loading replaces whatever was loaded before,
		// Existing event wrappers become invalid; scenes recreate their instances on the next update.
		// Returns false if any bank failed; successfully loaded banks remain available.
		//
		// The strings bank is loaded first and deliberately: it carries the path table, and without
		// it every "event:/..." lookup fails with a not-found that does not explain itself.
		static bool LoadBank(const std::filesystem::path& bankFile);
		static uint64_t GetBankRevision() { return s_BankRevision; }
		static bool LoadBanks(const std::filesystem::path& bankDirectory);
		// Loads only the exported manifest; any failure unloads the partial set.
		static bool LoadRuntimeBanks(const std::filesystem::path& assets, const AudioBankManifest& manifest);
		static void UnloadAllBanks();
		static const std::vector<AudioBankInfo>& GetLoadedBanks();

		// Every event the loaded banks describe, sorted by path. Rebuilt by LoadBanks; empty when
		// no banks are loaded.
		static const std::vector<AudioEventInfo>& GetEvents();

		// Changes whenever bank/system teardown invalidates Studio event handles. Main thread only.
		static uint64_t GetEventGeneration() { return s_EventGeneration; }

		// --- Mixer buses ------------------------------------------------------------------------
		//
		// The game-facing volume controls. busPath is an FMOD bus path ("bus:/", "bus:/SFX"), which
		// the sound designer defines in Studio - the engine does not invent the hierarchy, it only
		// drives what is authored. Volume is linear and nonnegative. Both return false / 0 when the bus does
		// not exist, which is the normal answer for a project that has not authored that bus.
		static std::string ResolveEventReference(const std::string& reference);
		static bool SetBusMuted(const std::string& path, bool muted);
		static bool SetVCAVolume(const std::string& path, float volume);
		static bool SetGlobalParameter(const std::string& name, float value);
		static float GetGlobalParameter(const std::string& name);
		// A volume set here is the game's: on the native backend it takes the bus over from mixer
		// snapshots until ClearBusVolume hands it back at the project mixer's volume. FMOD's
		// ClearBusVolume sets 1, the authored level.
		static bool SetBusVolume(const std::string& busPath, float volume);
		static bool ClearBusVolume(const std::string& busPath);
		static float GetBusVolume(const std::string& busPath);

		// --- Event parameters -------------------------------------------------------------------
		//
		// Looks up a writable local parameter of the event with this GUID in the loaded banks.
		// False when the event or parameter is absent, or the parameter is read-only, automatic or
		// global. Main thread only.
		static bool GetEventParameter(const std::string& eventGuid, const std::string& name, AudioEventParameterInfo& outInfo);
		// The label at index (0 = the parameter's minimum) of a labelled event parameter. On failure
		// outError describes why.
		static bool GetEventParameterLabel(const std::string& eventGuid, const std::string& name, int index,
			std::string& outLabel, std::string& outError);

		static bool ShuttingDownEngine() { return s_ShuttingDown; }

		static bool HasInitializedEngine() { return s_HasInitializedAudioEngine; }

	private:
		inline static bool s_HasInitializedAudioEngine = false;
		inline static bool s_ShuttingDown = false;
		static bool RefreshBankEvents();
		inline static uint64_t s_BankRevision = 0;
		inline static uint64_t s_EventGeneration = 0;
	};
}
