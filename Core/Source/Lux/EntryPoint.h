#pragma once

#include "Lux/Core/Application.h"

extern Lux::Application* Lux::CreateApplication(int argc, char** argv);
bool g_ApplicationRunning = true;

namespace Lux {

	int Main(int argc, char** argv)
	{
		while (g_ApplicationRunning)
		{
			InitializeCore();
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
	return LUX::Main(__argc, __argv);
}

#else

int main(int argc, char** argv)
{
	return Lux::Main(argc, argv);
}

#endif // LUX_DIST
