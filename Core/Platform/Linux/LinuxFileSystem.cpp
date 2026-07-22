#include "lpch.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Asset/AssetManager.h"

#include "Lux/Core/Application.h"

#include <GLFW/glfw3.h>

#include <sys/inotify.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <filesystem>
#include <thread>

namespace Lux {

	static std::filesystem::path s_PersistentStoragePath;

	FileStatus FileSystem::TryOpenFile(const std::filesystem::path& filepath)
	{
		int res = access(filepath.c_str(), F_OK);

		if (!res) return FileStatus::Success;

		switch (errno) {
		default: return FileStatus::OtherError;

		case ENOENT: [[fallthrough]];
		case ENOTDIR: return FileStatus::Invalid;

		case EPERM: [[fallthrough]];
		case EACCES: return FileStatus::Locked;
		}
	}

	bool FileSystem::WriteBytes(const std::filesystem::path& filepath, const Buffer& buffer)
	{
		std::ofstream stream(filepath, std::ios::binary | std::ios::trunc);

		if (!stream)
		{
			stream.close();
			return false;
		}

		stream.write((char*)buffer.Data, buffer.Size);
		stream.close();

		return true;
	}

	Buffer FileSystem::ReadBytes(const std::filesystem::path& filepath)
	{
		Buffer buffer;

		std::ifstream stream(filepath, std::ios::binary | std::ios::ate);
		LUX_CORE_ASSERT(stream);

		auto end = stream.tellg();
		stream.seekg(0, std::ios::beg);
		auto size = end - stream.tellg();
		LUX_CORE_ASSERT(size != 0);

		buffer.Allocate((uint32_t)size);
		stream.read((char*)buffer.Data, buffer.Size);
		stream.close();

		return buffer;
	}

	std::filesystem::path FileSystem::GetPersistentStoragePath()
	{
		if (!s_PersistentStoragePath.empty())
			return s_PersistentStoragePath;

		// Follow the XDG Base Directory spec: $XDG_DATA_HOME, else ~/.local/share. Mirrors the
		// Windows implementation, which roots persistent data at %APPDATA%/Editor.
		if (HasEnvironmentVariable("XDG_DATA_HOME"))
			s_PersistentStoragePath = GetEnvironmentVariable("XDG_DATA_HOME");
		else if (HasEnvironmentVariable("HOME"))
			s_PersistentStoragePath = std::filesystem::path(GetEnvironmentVariable("HOME")) / ".local" / "share";
		else
			s_PersistentStoragePath = "..";

		s_PersistentStoragePath /= "Editor";

		if (!std::filesystem::exists(s_PersistentStoragePath))
			std::filesystem::create_directories(s_PersistentStoragePath);

		return s_PersistentStoragePath;
	}

	bool FileSystem::HasEnvironmentVariable(const std::string& key)
	{
		return !GetEnvironmentVariable(key).empty();
	}

	bool FileSystem::SetEnvironmentVariable(const std::string& key, const std::string& value)
	{
		// NOTE(Emily): This looks horrible but the provided string *becomes* the environment for `putenv`
		// TODO(Emily): Persistent environment variables
		putenv((new std::string(std::format("{}={}", key, value)))->data());
		return true;
	}

	std::string FileSystem::GetEnvironmentVariable(const std::string& key)
	{
		const char* value = getenv(key.c_str());
		if (value)
			return std::string(value);
		else
			return {};
	}

}
