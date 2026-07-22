using System;

namespace Lux
{
	// A non-public field carrying this attribute is still exposed in the editor (mirrors Hazel's
	// ShowInEditorAttribute; checked in ScriptEngine::BuildAssemblyCache).
	[AttributeUsage(AttributeTargets.Field)]
	public class ShowInEditorAttribute : Attribute
	{
	}

	// Marks a managed type as an editor-assignable reference (Entity / asset handles). The engine
	// stores these fields as a UUID and reconstructs the wrapper on Instantiate (mirrors Hazel's
	// EditorAssignableAttribute; checked in ScriptEngine::Instantiate).
	[AttributeUsage(AttributeTargets.Class)]
	public class EditorAssignableAttribute : Attribute
	{
	}
}
