// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Obfuscation, not encryption (docs/OBFUSCATION_PLAN.md). Scrambles what a game ships so its files cannot be
// opened in an image viewer, a text editor or a bulk scraper. The key travels with the game, so a determined
// person can undo it; that is the stated limit.
//
// A scrambled file is a plain 32-byte header (ObfuscatedHeader), then the original bytes XORed with a keystream
// computed from (key, position in the original bytes) in 64-byte blocks. Any byte range can be read alone,
// which pack random access and audio streaming need, and the transform is a pure function, so it is safe on the
// asset, audio-streaming and job threads. Files without the header read as before.
namespace Lux::Obfuscation
{
	inline constexpr size_t KeySize = 32;
	inline constexpr size_t HeaderSize = 32;

	struct Key
	{
		std::array<uint8_t, KeySize> Bytes{};

		static Key Generate();
		// 64-bit identity of the key (not reversible), written in the header so a wrong key is reported
		// instead of producing garbage.
		uint64_t Fingerprint() const;
		std::string ToHex() const;
		// Exactly 64 hex digits.
		static std::optional<Key> FromHex(std::string_view hex);
		bool operator==(const Key&) const = default;
	};

	struct Header
	{
		char Magic[4] = { 'L', 'X', 'O', 'B' };
		uint16_t Version = 1;
		uint16_t Flags = 0;
		uint64_t KeyFingerprint = 0;
		uint8_t Reserved[16] = {};
	};
	static_assert(sizeof(Header) == HeaderSize);

	// XORs data in place with the keystream for [position, position + size) of the original bytes.
	// Applying it twice restores the input.
	void Apply(const Key& key, uint64_t position, void* data, size_t size);

	// Reads the header of a file. False when the file is missing, too short or not scrambled.
	bool Peek(const std::filesystem::path& path, Header& outHeader);
	// HeaderSize when the file is scrambled, else 0: where the original bytes start in the file.
	uint64_t DataStart(const std::filesystem::path& path);

	// The key every scrambled file is read with: set once on the main thread, at runtime start from the key
	// block the export patched in, or in the editor from the project being exported. Readers copy it when they
	// open a file, so later changes never affect an open stream.
	void SetActiveKey(const Key& key);
	void ClearActiveKey();
	std::optional<Key> GetActiveKey();

	// The key block: 64 bytes inside the player's executable that the export patches with the game's key.
	//   [0, 16)  magic, which the export searches the executable for (so it appears nowhere else in it);
	//   [16, 32) marker: KeyBlockUnpatched as built, KeyBlockPatched after the export;
	//   [32, 64) the key.
	inline constexpr size_t KeyBlockSize = 64;
	inline constexpr char KeyBlockMagic[17] = "LUXOBFKEYBLOCK01";
	inline constexpr char KeyBlockUnpatched[17] = "unpatched-------";
	inline constexpr char KeyBlockPatched[17] = "patched---KEY-ok";
	// The block as the player builds it: magic, "unpatched", then filler where the key goes. Spelled as bytes,
	// so the player holds the magic only here. The two spellings must agree (checked by the editor self-test).
#define LUX_OBFUSCATION_KEY_BLOCK_INIT 	'L','U','X','O','B','F','K','E','Y','B','L','O','C','K','0','1', 	'u','n','p','a','t','c','h','e','d','-','-','-','-','-','-','-', 	0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5, 	0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5
	// The key of a patched block, or none for an unpatched one. Reads through volatile, so a link-time
	// optimizer cannot fold the block to its build-time contents.
	std::optional<Key> ReadKeyBlock(const volatile uint8_t* block);
	// Writes the key into the single key block of an executable. False, with the reason, when the block is
	// missing or appears more than once.
	bool PatchExecutable(const std::filesystem::path& executable, const Key& key, std::string& outError);
}
