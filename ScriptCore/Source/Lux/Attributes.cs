// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	// A non-public field carrying this attribute is still exposed in the editor (mirrors Hazel's
	// ShowInEditorAttribute; checked in ScriptEngine::BuildAssemblyCache).
	/// <summary>Shows a private or protected field in the inspector (public fields show already).</summary>
	[AttributeUsage(AttributeTargets.Field)]
	public class ShowInEditorAttribute : Attribute
	{
	}

	// Marks a managed type as an editor-assignable reference (Entity / asset handles). The engine
	// stores these fields as a UUID and reconstructs the wrapper on Instantiate (mirrors Hazel's
	// EditorAssignableAttribute; checked in ScriptEngine::Instantiate).
	/// <summary>
	/// Marks a class whose fields the engine stores as an ID (an entity or an asset). The engine's own reference
	/// types carry it; scripts don't need it.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class EditorAssignableAttribute : Attribute
	{
	}

	// Editor-hint attributes read by ScriptEngine::BuildAssemblyCache (via Coral field reflection)
	// and applied in the inspector's Script section. Public fields, so the engine can read them back.

	/// <summary>Draws a numeric field as a slider clamped to [<see cref="Min"/>, <see cref="Max"/>].</summary>
	[AttributeUsage(AttributeTargets.Field)]
	public class RangeAttribute : Attribute
	{
		/// <summary>The smallest value the slider allows.</summary>
		public float Min;
		/// <summary>The largest value the slider allows.</summary>
		public float Max;

		/// <summary>A slider from <paramref name="min"/> to <paramref name="max"/>.</summary>
		public RangeAttribute(float min, float max)
		{
			Min = min;
			Max = max;
		}
	}

	/// <summary>Draws a bold label above the field, to group the fields that follow.</summary>
	[AttributeUsage(AttributeTargets.Field)]
	public class HeaderAttribute : Attribute
	{
		/// <summary>The label.</summary>
		public string Text;

		/// <summary>A header reading <paramref name="text"/>.</summary>
		public HeaderAttribute(string text)
		{
			Text = text;
		}
	}

	/// <summary>Shows help text when the field's label is hovered in the inspector.</summary>
	[AttributeUsage(AttributeTargets.Field)]
	public class TooltipAttribute : Attribute
	{
		/// <summary>The help text.</summary>
		public string Text;

		/// <summary>A tooltip reading <paramref name="text"/>.</summary>
		public TooltipAttribute(string text)
		{
			Text = text;
		}
	}
}
