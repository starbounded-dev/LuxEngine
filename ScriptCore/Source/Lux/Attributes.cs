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

	// Editor-hint attributes read by ScriptEngine::BuildAssemblyCache (via Coral field reflection)
	// and applied in the inspector's Script section. Public fields, so the engine can read them back.

	// Draws the field as a slider clamped to [Min, Max] (numeric fields only).
	[AttributeUsage(AttributeTargets.Field)]
	public class RangeAttribute : Attribute
	{
		public float Min;
		public float Max;

		public RangeAttribute(float min, float max)
		{
			Min = min;
			Max = max;
		}
	}

	// Draws a bold label above the field, to group related fields.
	[AttributeUsage(AttributeTargets.Field)]
	public class HeaderAttribute : Attribute
	{
		public string Text;

		public HeaderAttribute(string text)
		{
			Text = text;
		}
	}

	// Shows help text when the field label is hovered.
	[AttributeUsage(AttributeTargets.Field)]
	public class TooltipAttribute : Attribute
	{
		public string Text;

		public TooltipAttribute(string text)
		{
			Text = text;
		}
	}
}
