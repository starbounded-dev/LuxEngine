#include "lpch.h"
#include "AudioBankManifest.h"

#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"

#include <unordered_set>

namespace Lux
{
	namespace
	{
		constexpr uint32_t kMaxBanks = 4096;
		constexpr uint32_t kMaxPathBytes = 4096;

		bool ReadString(StreamReader& stream, std::string& value)
		{
			uint32_t length = 0;
			if (!stream.ReadData(reinterpret_cast<char*>(&length), sizeof(length)) || !stream.IsStreamGood() || length > kMaxPathBytes)
				return false;
			value.resize(length);
			return stream.ReadData(value.data(), length) && stream.IsStreamGood();
		}

		void WriteString(StreamWriter& stream, const std::string& value)
		{
			stream.WriteRaw<uint32_t>(static_cast<uint32_t>(value.size()));
			stream.WriteData(value.data(), value.size());
		}
	}

	bool AudioBankManifest::Validate() const
	{
		const std::string directory = Directory.generic_string();
		bool valid = directory.size() <= kMaxPathBytes && Banks.size() <= kMaxBanks
			&& (Directory.empty() == Banks.empty()) && !Directory.has_root_path()
			&& directory.find_first_of("\\:") == std::string::npos && directory.find('\0') == std::string::npos;
		for (const auto& part : Directory)
			valid = valid && part != ".." && (part != "." || Directory == ".");
		std::unordered_set<std::string> names;
		for (const auto& bank : Banks)
		{
			const std::filesystem::path path(bank);
			valid = valid && !bank.empty() && bank.size() <= kMaxPathBytes
				&& bank.find_first_of("/\\:") == std::string::npos && bank.find('\0') == std::string::npos
				&& path.extension() == ".bank" && names.insert(bank).second;
		}
		if (!valid)
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime bank manifest: expected an asset-relative directory and unique .bank filenames");
		return valid;
	}

	bool AudioBankManifest::Serialize(StreamWriter& stream) const
	{
		if (!Validate())
			return false;
		WriteString(stream, Directory.generic_string());
		stream.WriteRaw<uint8_t>(EnableLiveUpdate ? 1 : 0);
		stream.WriteRaw<uint32_t>(static_cast<uint32_t>(Banks.size()));
		for (const auto& bank : Banks)
			WriteString(stream, bank);
		if (!stream.IsStreamGood())
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to write runtime bank manifest");
			return false;
		}
		return true;
	}

	bool AudioBankManifest::Deserialize(StreamReader& stream)
	{
		AudioBankManifest result;
		std::string directory;
		uint8_t liveUpdate = 0;
		uint32_t count = 0;
		bool valid = ReadString(stream, directory)
			&& stream.ReadData(reinterpret_cast<char*>(&liveUpdate), sizeof(liveUpdate)) && stream.IsStreamGood()
			&& stream.ReadData(reinterpret_cast<char*>(&count), sizeof(count)) && stream.IsStreamGood()
			&& liveUpdate <= 1 && count <= kMaxBanks;
		if (valid)
		{
			result.Directory = directory;
			result.EnableLiveUpdate = liveUpdate != 0;
			result.Banks.resize(count);
			for (auto& bank : result.Banks)
			{
				if (!ReadString(stream, bank))
				{
					valid = false;
					break;
				}
			}
		}
		if (!valid)
		{
			LUX_CORE_ERROR_TAG("Audio", "Runtime bank manifest is truncated or exceeds supported limits");
			return false;
		}
		if (!result.Validate())
			return false;
		*this = std::move(result);
		return true;
	}
}
