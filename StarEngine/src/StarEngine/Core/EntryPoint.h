#pragma once
#include "StarEngine/Core/Base.h"
#include "StarEngine/Core/Application.h"

#ifdef SE_PLATFORM_WINDOWS

extern StarEngine::Application* StarEngine::CreateApplication(ApplicationCommandLineArgs args);

int main(int argc, char** argv)
{
	StarEngine::Log::Init();

	StarEngine::Application* app = StarEngine::CreateApplication({ argc, argv });
	SE_CORE_ASSERT(app, "Client Application is null!");
	app->Run();
	delete app;

	return 0;
}
#endif
