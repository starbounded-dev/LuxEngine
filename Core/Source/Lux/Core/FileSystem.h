#pragma once

#include "StarEngine/Core/Buffer.h"

namespace StarEngine {

	class FileSystem
	{
	public:
		// TODO: move to FileSystem class
		static Buffer ReadFileBinary(const std::filesystem::path& filepath);
	};

}
