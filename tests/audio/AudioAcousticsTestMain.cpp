// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>

namespace Lux
{
	decltype(Log::s_CoreLogger) Log::s_CoreLogger = spdlog::stdout_color_mt("soundwave-lifetime-test");
	decltype(Log::s_ClientLogger) Log::s_ClientLogger = Log::s_CoreLogger;
	decltype(Log::s_EditorConsoleLogger) Log::s_EditorConsoleLogger = Log::s_CoreLogger;
}

void RunAudioAcousticsTests();

int main()
{
	RunAudioAcousticsTests();
	std::cout << "PASS: isolated production Soundwave adapter lifetimes" << std::endl;
}
