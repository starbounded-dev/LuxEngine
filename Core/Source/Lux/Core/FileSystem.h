#pragma once

#include "Lux/Core/Buffer.h"

namespace Lux {

	class FileSystem
	{
	public:
		// TODO: move to FileSystem class
		static Buffer ReadFileBinary(const std::filesystem::path& filepath);
	};

}
