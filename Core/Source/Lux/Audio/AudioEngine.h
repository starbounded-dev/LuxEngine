// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/UUID.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// No backend type appears here: the implementation is Audio/Native/ (miniaudio; NativeInternal.h is
// its private header).
namespace Lux {

	class Sound;

	// Read-only backend playback statistics for editor tooling.
	struct AudioEngineStats
	{
		const char* BackendName = "None";
		bool Initialized = false;

		// Version of the audio library (miniaudio).
		uint32_t VersionMajor = 0;
		uint32_t VersionMinor = 0;
		uint32_t VersionPatch = 0;

		int SampleRate = 0;

		// False until mixer statistics are measured (not yet on miniaudio).
		bool HasMixerStats = false;
		int ChannelsPlaying = 0;
		int RealChannelsPlaying = 0;
		float DSPCPUPercent = 0.0f;
		float StreamCPUPercent = 0.0f;
		float UpdateCPUPercent = 0.0f;
		int MemoryCurrentBytes = 0;
		int MemoryPeakBytes = 0;
		int PlayingEventInstances = 0;
	};

	// A loaded Sound Bank and a clip in memory, as the Audio Debugger lists them.
	struct AudioLoadedBank
	{
		UUID Bank = 0;
		uint32_t Loads = 0;
		bool Resident = false;
		bool Ready = false;   // every clip it decodes is in memory
	};
	struct AudioLoadedClip
	{
		std::filesystem::path File;   // a loose file, or "luxpack/<handle><ext>" inside the asset pack
		uint32_t BankRefs = 0, LiveRefs = 0, Streams = 0;
		uint64_t Bytes = 0;           // the decoded buffer it holds; 0 while it streams or decodes
	};

	// A parameter a Sound defines (not a global one).
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
		// handles are Sound Bank asset handles. Main thread only.
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
		// A Sound asset was saved: its playing instances take the edit. Volume, pitch, curves,
		// parameters, attenuation and bus apply in place; a change to the tree, its clips or loops,
		// streaming or 3D on/off restarts each instance. Stopped instances use it on their next start.
		// Main thread only.
		static void ApplySoundEdit(const Sound& sound);
		// What is loaded now, for the editor. Main thread only.
		static std::vector<AudioLoadedBank> GetLoadedSoundBanks();
		static std::vector<AudioLoadedClip> GetLoadedClips();

		// Bumped by every engine restart, so holders of instances recreate them. Main thread only.
		static uint64_t GetBankRevision() { return s_BankRevision; }
		// Changes whenever an engine restart invalidates every instance. Main thread only.
		static uint64_t GetEventGeneration() { return s_EventGeneration; }

		// --- Mixer buses ------------------------------------------------------------------------
		//
		// The game-facing volume controls. busPath names a bus of the project's mixer ("bus:/",
		// "bus:/SFX"; View > Audio Mixer). Volume is linear and nonnegative. Both return false / 0 when
		// the bus does not exist, which is the normal answer for a project that has not authored that bus.
		static bool SetBusMuted(const std::string& path, bool muted);
		static bool SetVCAVolume(const std::string& path, float volume);
		static bool SetGlobalParameter(const std::string& name, float value);
		static float GetGlobalParameter(const std::string& name);
		// A volume set here is the game's: on the native backend it takes the bus over from mixer
		// snapshots until ClearBusVolume hands it back at the project mixer's volume.
		static bool SetBusVolume(const std::string& busPath, float volume);
		static bool ClearBusVolume(const std::string& busPath);
		static float GetBusVolume(const std::string& busPath);

		// --- Event parameters -------------------------------------------------------------------
		//
		// Looks up a parameter of the Sound this reference names (AudioEventRef::GetReference). False
		// when the Sound or parameter is absent, or the parameter is global. Main thread only.
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
		inline static uint64_t s_BankRevision = 0;
		inline static uint64_t s_EventGeneration = 0;
	};
}
