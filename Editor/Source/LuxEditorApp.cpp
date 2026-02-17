#include <Lux/EntryPoint.h>

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

	Application* CreateApplication(int argc, char** argv)
	{
		ApplicationSpecification spec;
		spec.Name = "LuxEditor";

		return new LuxEditor(spec);;
	}
}
