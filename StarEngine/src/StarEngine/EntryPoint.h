#pragma once

#include "StarEngine/Core/Application.h"

extern StarEngine::Application* StarEngine::CreateApplication(int argc, char** argv);
bool g_ApplicationRunning = true;

namespace StarEngine {

	int Main(int argc, char** argv)
	{
		while (g_ApplicationRunning)
		{
			InitializeCore();
			Application* app = CreateApplication(argc, argv);
			SE_CORE_ASSERT(app, "Client Application is null!");
			app->Run();
			delete app;
			ShutdownCore();
		}
		return 0;
	}

}

#if SE_DIST && SE_PLATFORM_WINDOWS

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
	return StarEngine::Main(__argc, __argv);
}

#else

int main(int argc, char** argv)
{
	return StarEngine::Main(argc, argv);
}

#endif // SE_DIST
