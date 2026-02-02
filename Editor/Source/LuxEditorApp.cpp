#include <Lux.h>

#include <Lux/Core/Entrypoint.h>

#include "EditorLayer.h"

namespace Lux {

	class LuxEditor : public Application
	{
	public:
		LuxEditor(const ApplicationSpecification& spec)
			: Application(spec)
		{
			PushLayer(new EditorLayer());
		}
		~LuxEditor()
		{

		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification spec;
		spec.Name = "LuxEditor";
		spec.CommandLineArgs = args;

		return new LuxEditor(spec);;
	}
}
