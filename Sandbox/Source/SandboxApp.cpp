#include <Lux.h>
#include <Lux/Core/EntryPoint.h>

#include "Lux/Renderer/OrthographicCameraController.h"
#include "Sandbox2D.h"

#include "ExampleLayer.h"

class Sandbox : public Lux::Application
{
public:
	Sandbox(const Lux::ApplicationSpecification& specification)
		: Lux::Application(specification)
	{
		//PushLayer(new ExampleLayer());
		PushLayer(new Sandbox2D());
	}

	~Sandbox()
	{

	}

};

Lux::Application* Lux::CreateApplication(Lux::ApplicationCommandLineArgs args)
{
	ApplicationSpecification spec;
	spec.Name = "Sandbox";
	spec.WorkingDirectory = "../Editor";
	spec.CommandLineArgs = args;

	return new Sandbox(spec);
}
