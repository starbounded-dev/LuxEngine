#pragma once
#include "Lux/Core/Base.h"
#include "Lux/Core/Application.h"

#ifdef LUX_PLATFORM_WINDOWS

extern Lux::Application* Lux::CreateApplication(ApplicationCommandLineArgs args);

int main(int argc, char** argv)
{
	Lux::Log::Init();

	Lux::Application* app = Lux::CreateApplication({ argc, argv });
	LUX_CORE_ASSERT(app, "Client Application is null!");
	app->Run();
	delete app;

	return 0;
}
#endif
