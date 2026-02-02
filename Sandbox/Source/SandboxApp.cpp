#include <StarEngine.h>
#include <StarEngine/Core/EntryPoint.h>

#include "StarEngine/Renderer/OrthographicCameraController.h"
#include "Sandbox2D.h"

#include "ExampleLayer.h"

class Sandbox : public StarEngine::Application
{
public:
	Sandbox(const StarEngine::ApplicationSpecification& specification)
		: StarEngine::Application(specification)
	{
		//PushLayer(new ExampleLayer());
		PushLayer(new Sandbox2D());
	}

	~Sandbox()
	{

	}

};

StarEngine::Application* StarEngine::CreateApplication(StarEngine::ApplicationCommandLineArgs args)
{
	ApplicationSpecification spec;
	spec.Name = "Sandbox";
	spec.WorkingDirectory = "../StarEditor";
	spec.CommandLineArgs = args;

	return new Sandbox(spec);
}
