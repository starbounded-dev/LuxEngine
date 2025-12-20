#include <StarEngine.h>

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
	};

	Application* CreateApplication(int argc, char** argv)
	{
		ApplicationSpecification spec;
		spec.Name = "StarEditor";
		return new StarEditor(spec);
	}

}

// keep this LAST
#include <StarEngine/EntryPoint.h>
