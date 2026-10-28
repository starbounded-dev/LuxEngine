#pragma once
#include "lpch.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cassert>
#include <iostream>
// Logging and Near() for headless audio tests. Tests that drive FMOD include AudioTestHost.h, which
// adds the FMOD host on top; tests of engine-only code include just this and need no audio SDK.
namespace Lux
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("test");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger = Log::s_CoreLogger;
} // namespace Lux
using namespace Lux;
void Near(float a, float b)
{
	if (std::abs(a - b) >= 0.0001f)
		std::cerr << "Expected " << b << ", got " << a << std::endl;
	assert(std::abs(a - b) < 0.0001f);
}
