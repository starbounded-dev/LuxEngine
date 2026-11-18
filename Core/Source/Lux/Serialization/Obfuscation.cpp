// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Obfuscation.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>

namespace Lux::Obfuscation
{
	namespace
	{
		constexpr uint64_t k_Golden = 0x9E3779B97F4A7C15ull;

		inline uint64_t SplitMix(uint64_t& state)
		{
			uint64_t z = (state += k_Golden);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		}

		inline uint64_t Word(const Key& key, size_t index)
		{
			uint64_t value;
			std::memcpy(&value, key.Bytes.data() + index * 8, 8);
			return value;
		}

		// The 64 keystream bytes of one block: mixing of (key, block index), not a cipher.
		void KeystreamBlock(const Key& key, uint64_t block, uint64_t out[8])
		{
			uint64_t state = Word(key, 0) ^ (block * 0xD6E8FEB86659FD93ull);
			state ^= Word(key, 1) + 0x632BE59BD9B4E019ull * (block + 1);
			uint64_t extra = Word(key, 2) ^ Word(key, 3);
			for (int i = 0; i < 8; ++i)
				out[i] = SplitMix(state) ^ (extra = (extra << 13 | extra >> 51) * 0xFF51AFD7ED558CCDull + block);
		}

		std::mutex s_ActiveKeyMutex;
		std::shared_ptr<const Key> s_ActiveKey;
	}

	Key Key::Generate()
	{
		std::random_device device;
		Key key;
		for (size_t i = 0; i < KeySize; i += 4)
		{
			const uint32_t value = device();
			std::memcpy(key.Bytes.data() + i, &value, 4);
		}
		return key;
	}

	uint64_t Key::Fingerprint() const
	{
		uint64_t state = 0x1F2E3D4C5B6A7988ull;
		uint64_t result = 0;
		for (size_t i = 0; i < 4; ++i)
		{
			state ^= Word(*this, i);
			result ^= SplitMix(state);
		}
		return result;
	}

	std::string Key::ToHex() const
	{
		static constexpr char digits[] = "0123456789abcdef";
		std::string text;
		text.reserve(KeySize * 2);
		for (uint8_t byte : Bytes)
		{
			text.push_back(digits[byte >> 4]);
			text.push_back(digits[byte & 15]);
		}
		return text;
	}

	std::optional<Key> Key::FromHex(std::string_view hex)
	{
		if (hex.size() != KeySize * 2)
			return std::nullopt;
		const auto digit = [](char c) -> int
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		Key key;
		for (size_t i = 0; i < KeySize; ++i)
		{
			const int high = digit(hex[i * 2]), low = digit(hex[i * 2 + 1]);
			if (high < 0 || low < 0)
				return std::nullopt;
			key.Bytes[i] = static_cast<uint8_t>(high << 4 | low);
		}
		return key;
	}

	void Apply(const Key& key, uint64_t position, void* data, size_t size)
	{
		auto* bytes = static_cast<uint8_t*>(data);
		while (size > 0)
		{
			const uint64_t block = position / 64;
			const size_t offset = static_cast<size_t>(position % 64);
			const size_t count = std::min<size_t>(64 - offset, size);
			uint64_t words[8];
			KeystreamBlock(key, block, words);
			const auto* stream = reinterpret_cast<const uint8_t*>(words);
			for (size_t i = 0; i < count; ++i)
				bytes[i] ^= stream[offset + i];
			bytes += count;
			position += count;
			size -= count;
		}
	}

	bool Peek(const std::filesystem::path& path, Header& outHeader)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		Header header;
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (file.gcount() != static_cast<std::streamsize>(sizeof(header)) || std::memcmp(header.Magic, "LXOB", 4) != 0)
			return false;
		outHeader = header;
		return true;
	}

	uint64_t DataStart(const std::filesystem::path& path)
	{
		Header header;
		return Peek(path, header) ? HeaderSize : 0;
	}

	void SetActiveKey(const Key& key)
	{
		std::scoped_lock lock(s_ActiveKeyMutex);
		s_ActiveKey = std::make_shared<const Key>(key);
	}

	void ClearActiveKey()
	{
		std::scoped_lock lock(s_ActiveKeyMutex);
		s_ActiveKey.reset();
	}

	std::optional<Key> GetActiveKey()
	{
		std::scoped_lock lock(s_ActiveKeyMutex);
		if (!s_ActiveKey)
			return std::nullopt;
		return *s_ActiveKey;
	}

	std::optional<Key> ReadKeyBlock(const volatile uint8_t* block)
	{
		// Compared byte by byte through the volatile pointer; the marker is never folded away.
		for (size_t i = 0; i < 16; ++i)
			if (block[16 + i] != static_cast<uint8_t>(KeyBlockPatched[i]))
				return std::nullopt;
		Key key;
		for (size_t i = 0; i < KeySize; ++i)
			key.Bytes[i] = block[32 + i];
		return key;
	}

	bool PatchExecutable(const std::filesystem::path& executable, const Key& key, std::string& outError)
	{
		std::fstream file(executable, std::ios::in | std::ios::out | std::ios::binary);
		if (!file)
		{
			outError = "Cannot open the executable to patch its key block: " + executable.string();
			return false;
		}
		file.seekg(0, std::ios::end);
		const auto length = static_cast<size_t>(file.tellg());
		std::string content(length, '\0');
		file.seekg(0);
		file.read(content.data(), static_cast<std::streamsize>(length));
		if (!file)
		{
			outError = "Cannot read the executable to patch its key block: " + executable.string();
			return false;
		}

		// Assembled from halves, never the 16-byte literal: a player that linked this function must contain the
		// magic once, in its key block, or the search below would find two.
		static const char k_MagicHead[] = "LUXOBFKE";
		static const char k_MagicTail[] = "YBLOCK01";
		const std::string magic = std::string(k_MagicHead) + k_MagicTail;
		const size_t first = content.find(magic);
		if (first == std::string::npos)
		{
			outError = "The player has no obfuscation key block, so this build cannot run an obfuscated game.";
			return false;
		}
		if (content.find(magic, first + 1) != std::string::npos)
		{
			outError = "The player has more than one obfuscation key block; the key cannot be placed safely.";
			return false;
		}

		std::array<uint8_t, KeyBlockSize - 16> tail{};
		std::memcpy(tail.data(), KeyBlockPatched, 16);
		std::memcpy(tail.data() + 16, key.Bytes.data(), KeySize);
		file.clear();
		file.seekp(static_cast<std::streamoff>(first + 16));
		file.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
		file.flush();
		if (!file)
		{
			outError = "Writing the key into the executable failed: " + executable.string();
			return false;
		}
		return true;
	}
}
