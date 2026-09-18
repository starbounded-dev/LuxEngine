#include "lpch.h"
#include "Lux/Serialization/FileStream.h"
#include <cassert>
#include <iostream>

int main(int argc, char** argv)
{
	assert(argc == 2);
	const auto path = std::filesystem::path(argv[1]) / "stream.bin";
	{
		Lux::FileStreamWriter writer(path);
		assert(writer.WriteData("test", 4));
		assert(writer.Flush());
	}
	Lux::FileStreamReader reader(path);
	char data[8]{};
	assert(reader.ReadData(data, 4));
	assert(std::string_view(data, 4) == "test");
	assert(!reader.ReadData(data, 1));
	assert(!reader.IsStreamGood());
	Lux::FileStreamWriter missing(path / "not-a-directory");
	assert(!missing.WriteData("test", 4));
	assert(!missing.Flush());
	std::cout << "PASS: file stream success, truncation and unwritable destination\n";
}
