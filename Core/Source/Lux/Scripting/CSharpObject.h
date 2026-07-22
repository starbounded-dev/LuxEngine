#pragma once

#include <Coral/ManagedObject.hpp>

#include <string_view>

namespace Lux {

	// Thin, non-owning wrapper over a Coral::ManagedObject* that lives in ScriptEngine's
	// StableVector. Adds by-name method invocation + validity; all field IO goes through
	// FieldStorage, not here. (Ported from Hazel's CSharpObject.)
	class CSharpObject
	{
	public:
		template<typename... TArgs>
		void Invoke(std::string_view methodName, TArgs&&... args)
		{
			LUX_CORE_VERIFY(m_Handle != nullptr);
			m_Handle->InvokeMethod(methodName, std::forward<TArgs>(args)...);
		}

		bool IsValid() const { return m_Handle != nullptr; }

		Coral::ManagedObject* GetHandle() const { return m_Handle; }

	private:
		Coral::ManagedObject* m_Handle = nullptr;

		friend class ScriptEngine;
	};

}
