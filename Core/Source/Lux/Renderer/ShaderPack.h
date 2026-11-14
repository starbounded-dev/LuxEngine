// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Shader.h"

#include "Lux/Serialization/Serialization.h"
#include "Lux/Serialization/ShaderPackFile.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Lux {

	// What produced the SPIR-V inside a pack, so a consumer with no shader compiler can tell
	// whether that pack still describes this build. A Dist config compiles nothing (Renderer/
	// Shader.h:16 defines LUX_HAS_SHADER_COMPILER as !LUX_DIST), and the LXSP v1 header is 16
	// fixed bytes read and written raw with no spare field (Serialization/ShaderPackFile.h:69-
	// 74; written at ShaderPack.cpp:142, read at :26) - so the identity lives in a sidecar beside
	// the pack ("<pack>.stamp") and the pack bytes stay readable by every exported package that
	// exists today (Lux-Runtime/src/RuntimeApplication.cpp:92-94, EditorLayer.cpp:398-407).
	struct ShaderPackStamp
	{
		// FNV chain over "path:contentFNV" of every shader source and the generated material-graph
		// include, walked sorted. Staleness detection - edit, add, remove, rename - not integrity:
		// a 32-bit digest is not a checksum, and a miss only ever asks for a regeneration.
		std::string m_ShaderInputs;
		// The global shader macros expanded into those sources (Renderer.h:338 GetGlobalShader
		// Macros) when the pack was written. Recorded for diagnosis, not compared: Init's defaults
		// are replaced from project settings before any frame. Sorted so the string is deterministic.
		std::string m_Macros;
		// Which GPU compiled the SPIR-V. A mismatch is reported rather than fatal: SPIR-V is
		// portable across drivers by the Vulkan spec, so another vendor is a difference to state,
		// whereas a source difference is a real mismatch (a stale pack would silently render the
		// old program, which Rendering.md § Validation errors are bugs forbids).
		std::string m_RendererName;
		uint32_t m_VendorID = 0;
		uint32_t m_DriverVersion = 0;
		// Program count from the pack header at authoring time: the structural check. ShaderPack.cpp:35
		// decides a pack is loaded on a magic match alone, so a header-valid-but-empty pack would
		// otherwise report success while providing nothing; a zero count makes that Unreadable.
		uint32_t m_Programs = 0;
	};

	// How the pack and sidecar on disk relate to a consuming environment. Names carry severity:
	// callers compare values, they never order them. NoStamp is not an error - the runtime-export
	// path (EditorLayer.cpp:398) has always written a bare pack, so existing packages only report.
	// InputsChanged must stop a compilerless consumer, which cannot rebuild the SPIR-V; DeviceChanged
	// is a warning only. Absent and unreadable packs are kept apart so the fix is findable.
	enum class ShaderPackState : uint8_t
	{
		Current,          // consume the SPIR-V in this pack
		InputsChanged,    // pack predates an edit to the shader sources
		DeviceChanged,    // authored on another GPU: consumable, reported
		NoStamp,          // unstamped pack (an exported package): reported, not validated
		Unreadable,       // header absent/truncated, or zero programs: structural fault
		Count             // every state is enumerable, so a switch cannot lean on a default
	};

	class ShaderPack : public RefCounted
	{
	public:
		ShaderPack() = default;
		ShaderPack(const std::filesystem::path& path);

		bool IsLoaded() const { return m_Loaded; }
		bool Contains(std::string_view name) const;

		Ref<Shader> LoadShader(std::string_view name);

		// The one spelling of the sidecar path, shared by ReadStamp and WriteStampedPack below -
		// the pairing rule Conventions.md § ImGui correctness gives popup/window names, applied to
		// the two files that describe one pack.
		[[nodiscard]] static std::filesystem::path StampPathFor(const std::filesystem::path& packPath);

		// Identity chain over every shader source and the generated material-graph include, walked
		// sorted. A 32-bit FNV chain: staleness detection, not integrity (see ShaderPackStamp
		// m_ShaderInputs). Main thread only - it reads the file system (Threading.md § Main: long
		// blocking work, file I/O).
		[[nodiscard]] static std::string ComputeShaderInputIdentity();

		// Read the sidecar; false when it is absent or unreadable, leaving stampOut untouched so the
		// caller can report "no stamp" separately from "a stamp that disagrees".
		[[nodiscard]] static bool ReadStamp(const std::filesystem::path& packPath, ShaderPackStamp& stampOut);

		// Author a pack and its sidecar from a complete shader library, atomically: the pack is
		// written to "<pack>.tmp" and swapped in, and the stamp lands only after that swap, so a
		// crash leaves the previous pair intact rather than a half-written pack that would then
		// pass the magic check (ShaderPack.cpp:26-35). False and logged on any failure; the temp
		// file is left for inspection but never promoted. stamp must describe the environment the
		// library was compiled in (Renderer::CaptureShaderPackEnvironment). Main thread: panel file
		// I/O (Threading.md § Main). It creates no GPU resource - the SPIR-V belongs to the RHIShaders
		// the library already owns (Rendering.md § Ownership and lifetime).
		[[nodiscard]] static bool WriteStampedPack(Ref<ShaderLibrary> shaderLibrary, const std::filesystem::path& packPath, ShaderPackStamp stamp);

		// How the pack + sidecar on disk relate to a consuming environment. Main thread: it reads
		// the file system and the shader sources. NoStamp keeps the bare-pack export
		// path working; Unreadable covers an absent pack (EditorLayer.cpp:398); InputsChanged stops a compilerless consumer; DeviceChanged
		// warns, because SPIR-V is portable across drivers by spec.
		[[nodiscard]] static ShaderPackState Evaluate(const std::filesystem::path& packPath, const ShaderPackStamp& environment);

		static Ref<ShaderPack> CreateFromLibrary(Ref<ShaderLibrary> shaderLibrary, const std::filesystem::path& path);

	private:
		bool m_Loaded = false;
		ShaderPackFile m_File;
		std::filesystem::path m_Path;
	};

}
