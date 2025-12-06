#include "sepch.h"
#include "FileSystem.h"
#include "StringUtils.h"
#include "StarEngine/Core/Log.h"

#include <filesystem>
#include <fstream>

#ifdef SE_PLATFORM_LINUX
#include <libgen.h>
#endif

#include <nfd.hpp>
#include <format>

namespace StarEngine {

	std::filesystem::path FileSystem::GetWorkingDirectory()
	{
		return std::filesystem::current_path();
	}

	void FileSystem::SetWorkingDirectory(std::filesystem::path path)
	{
		std::filesystem::current_path(path);
	}

	bool FileSystem::CreateDirectory(const std::filesystem::path& directory)
	{
		return std::filesystem::create_directories(directory);
	}

	bool FileSystem::CreateDirectory(const std::string& directory)
	{
		return CreateDirectory(std::filesystem::path(directory));
	}

	bool FileSystem::Move(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath)
	{
		if (FileSystem::Exists(newFilepath))
			return false;

		std::filesystem::rename(oldFilepath, newFilepath);
		return true;
	}

	bool FileSystem::Copy(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath)
	{
		if (FileSystem::Exists(newFilepath))
			return false;

		std::filesystem::copy(oldFilepath, newFilepath);
		return true;
	}

	bool FileSystem::MoveFile(const std::filesystem::path& filepath, const std::filesystem::path& dest)
	{
		return Move(filepath, dest / filepath.filename());
	}

	bool FileSystem::CopyFile(const std::filesystem::path& filepath, const std::filesystem::path& dest)
	{
		return Copy(filepath, dest / filepath.filename());
	}

	bool FileSystem::Rename(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath)
	{
		return Move(oldFilepath, newFilepath);
	}

	bool FileSystem::RenameFilename(const std::filesystem::path& oldFilepath, const std::string& newName)
	{
		std::filesystem::path newPath = oldFilepath.parent_path()
			/ std::filesystem::path(newName + oldFilepath.extension().string());
		return Rename(oldFilepath, newPath);
	}

	bool FileSystem::Exists(const std::filesystem::path& filepath)
	{
		return std::filesystem::exists(filepath);
	}

	bool FileSystem::Exists(const std::string& filepath)
	{
		return std::filesystem::exists(std::filesystem::path(filepath));
	}

	bool FileSystem::DeleteFile(const std::filesystem::path& filepath)
	{
		if (!FileSystem::Exists(filepath))
			return false;

		if (std::filesystem::is_directory(filepath))
			return std::filesystem::remove_all(filepath) > 0;
		return std::filesystem::remove(filepath);
	}

	bool FileSystem::IsDirectory(const std::filesystem::path& filepath)
	{
		return std::filesystem::is_directory(filepath);
	}

	FileStatus FileSystem::TryOpenFileAndWait(const std::filesystem::path& filepath, uint64_t waitms)
	{
		FileStatus fileStatus = TryOpenFile(filepath);
		if (fileStatus == FileStatus::Locked)
		{
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(std::chrono::milliseconds(waitms));
			return TryOpenFile(filepath);
		}
		return fileStatus;
	}

	bool FileSystem::IsNewer(const std::filesystem::path& fileA, const std::filesystem::path& fileB)
	{
		return std::filesystem::last_write_time(fileA) > std::filesystem::last_write_time(fileB);
	}

	bool FileSystem::ShowFileInExplorer(const std::filesystem::path& path)
	{
		auto absolutePath = std::filesystem::canonical(path);
		if (!Exists(absolutePath))
			return false;

#ifdef SE_PLATFORM_WINDOWS
		std::string cmd = std::format("explorer.exe /select,\"{}\"", absolutePath.string());
		system(cmd.c_str());
		return true;
#elif defined(SE_PLATFORM_LINUX)
		std::string cmd = std::format("xdg-open \"{}\"", dirname(absolutePath.string().data()));
		system(cmd.c_str());
		return true;
#endif
	}

	bool FileSystem::OpenDirectoryInExplorer(const std::filesystem::path& path)
	{
		auto absolutePath = std::filesystem::canonical(path);
		if (!Exists(absolutePath))
			return false;

#ifdef SE_PLATFORM_WINDOWS
		// Open folder in Explorer using system() instead of ShellExecute
		std::string cmd = std::format("explorer.exe \"{}\"", absolutePath.string());
		system(cmd.c_str());
		return true;
#elif defined(SE_PLATFORM_LINUX)
		std::string cmd = std::format("xdg-open \"{}\"", absolutePath.string().data());
		system(cmd.c_str());
		return true;
#endif
	}

	bool FileSystem::OpenExternally(const std::filesystem::path& path)
	{
		auto absolutePath = std::filesystem::canonical(path);
		if (!Exists(absolutePath))
			return false;

#ifdef SE_PLATFORM_WINDOWS
		// Use start "" "<file>" to open with default app
		std::string cmd = std::format("start \"\" \"{}\"", absolutePath.string());
		system(cmd.c_str());
		return true;
#elif defined(SE_PLATFORM_LINUX)
		std::string cmd = std::format("xdg-open \"{}\"", absolutePath.string().data());
		system(cmd.c_str());
		return true;
#endif
	}

	std::filesystem::path FileSystem::GetUniqueFileName(const std::filesystem::path& filepath)
	{
		if (!FileSystem::Exists(filepath))
			return filepath;

		int counter = 0;
		auto checkID = [&counter, filepath](auto checkID) -> std::filesystem::path
			{
				++counter;
				const std::string counterStr = [&counter] {
					if (counter < 10)
						return "0" + std::to_string(counter);
					else
						return std::to_string(counter);
					}();

				std::string newFileName = std::format(
					"{} ({})",
					Utils::RemoveExtension(filepath.filename().string()),
					counterStr
				);

				if (filepath.has_extension())
					newFileName = std::format("{}{}", newFileName, filepath.extension().string());

				if (std::filesystem::exists(filepath.parent_path() / newFileName))
					return checkID(checkID);
				else
					return filepath.parent_path() / newFileName;
			};

		return checkID(checkID);
	}

	uint64_t FileSystem::GetLastWriteTime(const std::filesystem::path& filepath)
	{
		SE_CORE_ASSERT(FileSystem::Exists(filepath));

		if (TryOpenFileAndWait(filepath) == FileStatus::Success)
		{
			std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(filepath);
			return std::chrono::duration_cast<std::chrono::seconds>(lastWriteTime.time_since_epoch()).count();
		}

		SE_CORE_ERROR("FileSystem::GetLastWriteTime - could not open file: {}", filepath.string());
		return 0;
	}

	std::filesystem::path FileSystem::OpenFileDialog(const std::initializer_list<FileDialogFilterItem> inFilters)
	{
		NFD::UniquePath filePath;
		nfdresult_t result = NFD::OpenDialog(filePath,
			(const nfdfilteritem_t*)inFilters.begin(), inFilters.size());

		switch (result)
		{
		case NFD_OKAY:   return filePath.get();
		case NFD_CANCEL: return "";
		case NFD_ERROR:
		{
			// Avoid SE_CORE_VERIFY to dodge the _CORE_ macro problem
			SE_CORE_ERROR("NFD-Extended threw an error: {}", NFD::GetError());
			return "";
		}
		}

		return "";
	}

	std::filesystem::path FileSystem::OpenFolderDialog(const char* initialFolder)
	{
		NFD::UniquePath filePath;
		nfdresult_t result = NFD::PickFolder(filePath, initialFolder);

		switch (result)
		{
		case NFD_OKAY:   return filePath.get();
		case NFD_CANCEL: return "";
		case NFD_ERROR:
		{
			SE_CORE_ERROR("NFD-Extended threw an error: {}", NFD::GetError());
			return "";
		}
		}

		return "";
	}

	std::filesystem::path FileSystem::SaveFileDialog(const std::initializer_list<FileDialogFilterItem> inFilters)
	{
		NFD::UniquePath filePath;
		nfdresult_t result = NFD::SaveDialog(filePath,
			(const nfdfilteritem_t*)inFilters.begin(), inFilters.size());

		switch (result)
		{
		case NFD_OKAY:   return filePath.get();
		case NFD_CANCEL: return "";
		case NFD_ERROR:
		{
			SE_CORE_ERROR("NFD-Extended threw an error: {}", NFD::GetError());
			return "";
		}
		}

		return "";
	}

	FileStatus FileSystem::TryOpenFile(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return FileStatus::Invalid;

		std::ifstream f(path, std::ios::binary);
		if (f.good())
			return FileStatus::Success;

		return FileStatus::Locked;
	}

	Buffer FileSystem::ReadBytes(const std::filesystem::path& filepath)
	{
		Buffer buffer;

		// Open at end to get file size
		std::ifstream stream(filepath, std::ios::binary | std::ios::ate);
		if (!stream)
		{
			SE_CORE_ERROR("Failed to open file for reading bytes: {}", filepath.string());
			return buffer; // empty
		}

		std::streampos end = stream.tellg();
		if (end <= 0)
			return buffer;

		buffer.Allocate(static_cast<uint64_t>(end));

		stream.seekg(0, std::ios::beg);
		stream.read(reinterpret_cast<char*>(buffer.Data), buffer.Size);
		stream.close();

		return buffer;
	}


}
