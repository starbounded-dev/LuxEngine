// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "StreamReader.h"

namespace Lux
{
	bool StreamReader::ReadBuffer(Buffer& buffer, uint32_t size)
	{
		buffer.Size = size;
		if (size == 0 && !ReadData((char*)&buffer.Size, sizeof(uint64_t)))
		{
			buffer.Size = 0;
			return false;
		}
		if (buffer.Size > GetRemainingBytes())
		{
			LUX_CORE_ERROR("StreamReader: a {}-byte buffer runs past the end of the stream", buffer.Size);
			buffer.Size = 0;
			return false;
		}

		buffer.Allocate(buffer.Size);
		if (!ReadData((char*)buffer.Data, buffer.Size))
		{
			buffer.Release();
			return false;
		}
		return true;
	}

	bool StreamReader::ReadString(std::string& string)
	{
		size_t size = 0;
		if (!ReadData((char*)&size, sizeof(size_t)) || size > GetRemainingBytes())
		{
			string.clear();
			return false;
		}

		string.resize(size);
		if (!ReadData((char*)string.data(), sizeof(char) * size))
		{
			string.clear();
			return false;
		}
		return true;
	}

} // namespace Lux
