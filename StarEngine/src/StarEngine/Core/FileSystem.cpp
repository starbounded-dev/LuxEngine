#include "sepch.h"
#include "FileSystem.h"

#include <fstream> // Ensure this header is included for std::ifstream

namespace StarEngine {

	Buffer FileSystem::ReadFileBinary(const std::filesystem::path& filepath)
	{
		// Correctly initialize std::ifstream
		std::ifstream stream(filepath.string(), std::ios::binary | std::ios::ate);

		if (!stream)
		{
			// Failed to open the file
			return {};
		}

		std::streampos end = stream.tellg();
		stream.seekg(0, std::ios::beg);
		uint64_t size = end - stream.tellg();

		if (size == 0)
		{
			// File is empty
			return {};
		}

		Buffer buffer(size);
		stream.read(buffer.As<char>(), size);
		stream.close();
		return buffer;
	}

}
