#include "lpch.h"
#include "FileDialogs.h"

#include "Lux/Utilities/FileSystem.h"

namespace Lux {

	std::string FileDialogs::OpenFile(const char* /*filter*/)
	{
		// TODO: parse the filter string if you want, for now just a sane default
		auto path = FileSystem::OpenFileDialog({ { "All Files", "*" } });
		return path.empty() ? std::string{} : path.string();
	}

	std::string FileDialogs::SaveFile(const char* /*filter*/)
	{
		auto path = FileSystem::SaveFileDialog({ { "All Files", "*" } });
		return path.empty() ? std::string{} : path.string();
	}

	std::string FileDialogs::OpenFolder()
	{
		auto path = FileSystem::OpenFolderDialog();
		return path.empty() ? std::string{} : path.string();
	}

}
