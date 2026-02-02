#include <StarEngine.h>

#include <StarEngine/Core/Entrypoint.h>

#include "EditorLayer.h"

namespace StarEngine {

	class StarEditor : public Application
	{
	public:
		StarEditor(const ApplicationSpecification& spec)
			: Application(spec)
		{
			PushLayer(new EditorLayer());
		}
		~StarEditor()
		{

		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification spec;
		spec.Name = "StarEditor";
		spec.CommandLineArgs = args;

		return new StarEditor(spec);;
	}
}
