#pragma once

namespace Coral {
	class ManagedAssembly;
}

namespace Lux {

	class ScriptGlue
	{
	public:
		// Registers component add/has/remove maps and all internal calls onto the core
		// assembly, then uploads them. Called once per assembly load/reload.
		static void RegisterGlue(Coral::ManagedAssembly& coreAssembly);
	};

}
