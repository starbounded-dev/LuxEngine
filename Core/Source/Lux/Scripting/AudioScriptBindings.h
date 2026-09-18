#pragma once

namespace Coral { class ManagedAssembly; }

namespace Lux {
	class AudioScriptBindings
	{
	public:
		static void Register(Coral::ManagedAssembly& assembly);
		static void Update(bool paused);
		static void Reset();
		static void Shutdown();
	};
}
