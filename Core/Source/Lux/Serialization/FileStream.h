// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "StreamWriter.h"
#include "StreamReader.h"
#include "Lux/Core/Buffer.h"
#include "Obfuscation.h"

#include <filesystem>
#include <fstream>

namespace Lux
{
	//==============================================================================
	/// FileStreamWriter
	class FileStreamWriter : public StreamWriter
	{
	public:
		// With a key the file is a scrambled container (Obfuscation.h): the header, then the bytes written
		// here scrambled. Positions stay those of the bytes written, so formats with offsets need no change.
		FileStreamWriter(const std::filesystem::path& path, const Obfuscation::Key* key = nullptr);
		FileStreamWriter(const FileStreamWriter&) = delete;
		virtual ~FileStreamWriter();

		bool IsStreamGood() const final { return m_Stream.good(); }
		uint64_t GetStreamPosition() final { return static_cast<uint64_t>(m_Stream.tellp()) - m_DataStart; }
		void SetStreamPosition(uint64_t position) final { m_Stream.seekp(position + m_DataStart); }
		bool WriteData(const char* data, size_t size) final;
		bool Flush();

	private:
		std::filesystem::path m_Path;
		std::ofstream m_Stream;
		std::optional<Obfuscation::Key> m_Key;
		uint64_t m_DataStart = 0;
	};

	//==============================================================================
	/// FileStreamReader
	class FileStreamReader : public StreamReader
	{
	public:
		// A scrambled container is read with the active key (Obfuscation::SetActiveKey), transparently: the
		// header is hidden and positions are those of the original bytes. Without the right key the stream is
		// not good, and the log says why.
		FileStreamReader(const std::filesystem::path& path);
		FileStreamReader(const FileStreamReader&) = delete;
		~FileStreamReader();

		bool IsStreamGood() const final { return !m_Failed && m_Stream.good(); }
		uint64_t GetStreamPosition() override { return static_cast<uint64_t>(m_Stream.tellg()) - m_DataStart; }
		void SetStreamPosition(uint64_t position) override { m_Stream.seekg(position + m_DataStart); }
		bool ReadData(char* destination, size_t size) override;
		uint64_t GetStreamSize() override { return m_Size; }
		const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		std::filesystem::path m_Path;
		std::ifstream m_Stream;
		uint64_t m_Size = 0;
		std::optional<Obfuscation::Key> m_Key;
		uint64_t m_DataStart = 0;
		bool m_Failed = false;
	};

} // namespace Lux
