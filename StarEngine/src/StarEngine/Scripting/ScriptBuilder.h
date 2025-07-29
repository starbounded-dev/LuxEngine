#pragma once

#include "StarEngine/Project/Project.h"

namespace StarEngine {

	class ScriptBuilder
	{
	public:
		static void BuildCSProject(const std::filesystem::path& filepath);
		static void BuildScriptAssembly(Ref<Project> project);
	};

}
