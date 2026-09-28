// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Application.h"
#include "Lux/Utilities/FileSystem.h"

// Define LUX_LOGS_IN_PERSISTENT_STORAGE before including this header to write logs to the per-user
// folder (FileSystem::GetPersistentStoragePath()/logs) instead of ./logs. The editor does; shipped
// games (Lux-Runtime) keep their logs next to the game.

extern Lux::Application* Lux::CreateApplication(int argc, char** argv);
bool g_ApplicationRunning = true;

namespace Lux {

	int Main(int argc, char** argv)
	{
		while (g_ApplicationRunning)
		{
#ifdef LUX_LOGS_IN_PERSISTENT_STORAGE
			InitializeCore(FileSystem::GetPersistentStoragePath() / "logs");
#else
			InitializeCore();
#endif
			Application* app = CreateApplication(argc, argv);
			LUX_CORE_ASSERT(app, "Client Application is null!");
			app->Run();
			delete app;
			ShutdownCore();
		}
		return 0;
	}

}

#if LUX_DIST && LUX_PLATFORM_WINDOWS

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
	return Lux::Main(__argc, __argv);
}

#else

int main(int argc, char** argv)
{
	return Lux::Main(argc, argv);
}

#endif // LUX_DIST
