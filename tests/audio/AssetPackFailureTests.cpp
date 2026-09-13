#include "lpch.h"
#include "Lux/Serialization/AssetPackSerializer.h"
#include "Lux/Asset/AssetImporter.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cassert>
#include <iostream>

namespace Lux
{
	// Host seams only: exercise the production pack writer with failing asset serializers.
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("pack-test");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	uint64_t FailingHandle = 0;
	bool AssetImporter::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& info)
	{
		if (handle == FailingHandle)
			return false;
		info.Offset = stream.GetStreamPosition();
		info.Size = 4;
		return stream.WriteData("test", 4);
	}
} // namespace Lux

int main(int argc, char** argv)
{
	assert(argc == 2);
	using namespace Lux;
	const auto path = std::filesystem::path(argv[1]) / "pack-test.lap";
	AssetPackFile pack;
	pack.Index.Scenes[1].Assets[2] = {};
	std::atomic<float> progress = 0;
	FailingHandle = 1;
	assert(!AssetPackSerializer::Serialize(path, pack, {}, progress));
	FailingHandle = 2;
	assert(!AssetPackSerializer::Serialize(path, pack, {}, progress));
	FailingHandle = 0;
	assert(AssetPackSerializer::Serialize(path, pack, {}, progress));
	assert(!AssetPackSerializer::Serialize(path / "not-a-directory", pack, {}, progress));
	std::cout << "PASS: asset pack propagates failed scenes/assets and unwritable destinations\n";
}
