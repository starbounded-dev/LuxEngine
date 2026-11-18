// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "FileStream.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Lux
{
	//==============================================================================
	/// FileStreamWriter
	FileStreamWriter::FileStreamWriter(const std::filesystem::path& path, const Obfuscation::Key* key)
		: m_Path(path)
	{
		m_Stream = std::ofstream(path, std::ofstream::out | std::ofstream::binary);
		if (key)
		{
			m_Key = *key;
			m_DataStart = Obfuscation::HeaderSize;
			Obfuscation::Header header;
			header.KeyFingerprint = key->Fingerprint();
			m_Stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
		}
	}

	FileStreamWriter::~FileStreamWriter()
	{
		m_Stream.close();
	}

	bool FileStreamWriter::Flush()
	{
		m_Stream.flush();
		return m_Stream.good();
	}

	bool FileStreamWriter::WriteData(const char* data, size_t size)
	{
		if (!m_Key)
		{
			m_Stream.write(data, size);
			return m_Stream.good();
		}

		// Scrambled in pieces, so a large buffer is not copied whole.
		const uint64_t start = GetStreamPosition();
		constexpr size_t k_Piece = 1u << 16;
		std::vector<char> piece(std::min(size, k_Piece));
		for (size_t done = 0; done < size && m_Stream.good();)
		{
			const size_t count = std::min(size - done, k_Piece);
			std::memcpy(piece.data(), data + done, count);
			Obfuscation::Apply(*m_Key, start + done, piece.data(), count);
			m_Stream.write(piece.data(), static_cast<std::streamsize>(count));
			done += count;
		}
		return m_Stream.good();
	}

	//==============================================================================
	/// FileStreamReader
	FileStreamReader::FileStreamReader(const std::filesystem::path& path)
		: m_Path(path)
	{
		m_Stream = std::ifstream(path, std::ifstream::in | std::ifstream::binary);
		std::error_code error;
		m_Size = std::filesystem::file_size(path, error);
		if (error)
			m_Size = 0;

		Obfuscation::Header header;
		if (Obfuscation::Peek(path, header))
		{
			m_Key = Obfuscation::GetActiveKey();
			if (!m_Key)
			{
				LUX_CORE_ERROR_TAG("Obfuscation", "'{}' is scrambled but no key is available; this game was exported with obfuscation and its executable carries no key.", path.string());
				m_Failed = true;
			}
			else if (m_Key->Fingerprint() != header.KeyFingerprint)
			{
				LUX_CORE_ERROR_TAG("Obfuscation", "'{}' was scrambled with a different key than the one in use.", path.string());
				m_Failed = true;
			}
			m_DataStart = Obfuscation::HeaderSize;
			m_Size = m_Size >= m_DataStart ? m_Size - m_DataStart : 0;
			m_Stream.seekg(static_cast<std::streamoff>(m_DataStart));
		}
	}

	FileStreamReader::~FileStreamReader()
	{
		m_Stream.close();
	}

	bool FileStreamReader::ReadData(char* destination, size_t size)
	{
		if (m_Failed)
			return false;
		const uint64_t position = m_Key ? GetStreamPosition() : 0;
		m_Stream.read(destination, size);
		const bool good = m_Stream.good();
		if (m_Key && good)
			Obfuscation::Apply(*m_Key, position, destination, size);
		return good;
	}

} // namespace Lux
