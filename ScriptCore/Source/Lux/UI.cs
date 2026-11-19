// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Collections.Generic;

using Coral.Managed.Interop;

namespace Lux
{
	// Field numbers shared with native UIScriptBindings, in the same order.
	internal enum UIFloatField { WidgetValue = 0, WidgetMin, WidgetMax, ScrollOffset, TextSize }
	internal enum UIBoolField { ElementVisible = 0, WidgetInteractable, WidgetChecked, WidgetWholeNumbers, WidgetHovered, WidgetPressed, WidgetFocused, CanvasEnabled }
	internal enum UIVectorField { ImageColor = 0, TextColor, AnchorMin, AnchorMax, OffsetMin, OffsetMax }
	internal enum UITextField { Text = 0, Placeholder }

	/// <summary>
	/// Game UI events that are not about one widget. A widget's own events (<see cref="UIWidgetComponent.Clicked"/> and the
	/// others) are on its component.
	/// </summary>
	public static class UI
	{
		private static Action? s_Cancelled;

		/// <summary>
		/// Raised when the player presses Cancel (Escape, or the east button of a gamepad) while no text input is being
		/// edited: the place to close a menu. Handlers are dropped when the scene ends.
		/// </summary>
		public static event Action Cancelled
		{
			add => s_Cancelled += value;
			remove => s_Cancelled -= value;
		}

		internal static void RaiseCancelled() => UIEvents.Raise(s_Cancelled);
		internal static void ResetEvents() => s_Cancelled = null;
	}

	// Where the handlers scripts registered on widgets live, and where the engine delivers each frame's UI events. One
	// handler that throws is logged and never stops the others or the frame.
	internal static class UIEvents
	{
		internal sealed class Handlers
		{
			public Action? Clicked;
			public Action<bool>? Toggled;
			public Action<float>? ValueChanged;
			public Action<string>? TextChanged;
			public Action<string>? Submitted;
			public Action? Cancelled;
		}

		private static readonly Dictionary<ulong, Handlers> s_Handlers = new Dictionary<ulong, Handlers>();

		internal static Handlers For(ulong entity)
		{
			if (!s_Handlers.TryGetValue(entity, out Handlers? handlers))
				s_Handlers[entity] = handlers = new Handlers();
			return handlers;
		}

		internal static void Raise(Action? handlers)
		{
			if (handlers == null)
				return;
			foreach (Delegate handler in handlers.GetInvocationList())
			{
				try { ((Action)handler)(); }
				catch (Exception exception) { Log.Error($"A UI event handler threw: {exception}"); }
			}
		}

		private static void Raise<T>(Action<T>? handlers, T argument)
		{
			if (handlers == null)
				return;
			foreach (Delegate handler in handlers.GetInvocationList())
			{
				try { ((Action<T>)handler)(argument); }
				catch (Exception exception) { Log.Error($"A UI event handler threw: {exception}"); }
			}
		}

		// Called by the engine: kind is native UI::EventKind (0 Click, 1 Toggled, 2 ValueChanged, 3 TextChanged, 4 Submit, 5 Cancel).
		internal static void Dispatch(ulong entity, int kind, float value, int flag, string text)
		{
			s_Handlers.TryGetValue(entity, out Handlers? handlers);
			switch (kind)
			{
				case 0: Raise(handlers?.Clicked); break;
				case 1: Raise(handlers?.Toggled, flag != 0); break;
				case 2: Raise(handlers?.ValueChanged, value); break;
				case 3: Raise(handlers?.TextChanged, text); break;
				case 4: Raise(handlers?.Submitted, text); break;
				case 5:
					Raise(handlers?.Cancelled);
					UI.RaiseCancelled();
					break;
			}
		}

		internal static void Reset()
		{
			s_Handlers.Clear();
			UI.ResetEvents();
		}
	}

	/// <summary>The root of a user interface: the elements under it are drawn over the game, scaled to the screen.</summary>
	public unsafe class UICanvasComponent : Component
	{
		/// <summary>The same switch as <see cref="Component.Enabled"/>: a canvas that is off is neither drawn nor operated.</summary>
		public bool Visible
		{
			get => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.CanvasEnabled);
			set => InternalCalls.UI_SetBool(Entity.ID, (int)UIBoolField.CanvasEnabled, value);
		}
	}

	/// <summary>A rectangle in a user interface, placed inside its parent by anchors and offsets.</summary>
	public unsafe class UIElementComponent : Component
	{
		/// <summary>Whether the element and everything under it is shown and takes part in layout.</summary>
		public bool Visible
		{
			get => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.ElementVisible);
			set => InternalCalls.UI_SetBool(Entity.ID, (int)UIBoolField.ElementVisible, value);
		}

		/// <summary>The bottom-left anchor, as a fraction of the parent (0, 0 is its bottom left corner).</summary>
		public Vector2 AnchorMin { get => GetVector(UIVectorField.AnchorMin); set => SetVector(UIVectorField.AnchorMin, value); }

		/// <summary>The top-right anchor, as a fraction of the parent (1, 1 is its top right corner).</summary>
		public Vector2 AnchorMax { get => GetVector(UIVectorField.AnchorMax); set => SetVector(UIVectorField.AnchorMax, value); }

		/// <summary>Distance in UI units from the minimum anchor to the element's bottom-left corner.</summary>
		public Vector2 OffsetMin { get => GetVector(UIVectorField.OffsetMin); set => SetVector(UIVectorField.OffsetMin, value); }

		/// <summary>Distance in UI units from the maximum anchor to the element's top-right corner.</summary>
		public Vector2 OffsetMax { get => GetVector(UIVectorField.OffsetMax); set => SetVector(UIVectorField.OffsetMax, value); }

		private Vector2 GetVector(UIVectorField field)
		{
			Vector4 value;
			InternalCalls.UI_GetVector(Entity.ID, (int)field, &value);
			return new Vector2(value.X, value.Y);
		}

		private void SetVector(UIVectorField field, Vector2 value)
		{
			Vector4 packed = new Vector4(value.X, value.Y, 0.0f, 0.0f);
			InternalCalls.UI_SetVector(Entity.ID, (int)field, &packed);
		}
	}

	/// <summary>Fills an element with a color or a texture.</summary>
	public unsafe class UIImageComponent : Component
	{
		/// <summary>Color (RGBA, 0 to 1); tints the texture when there is one.</summary>
		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.UI_GetVector(Entity.ID, (int)UIVectorField.ImageColor, &c); return c; }
			set { InternalCalls.UI_SetVector(Entity.ID, (int)UIVectorField.ImageColor, &value); }
		}
	}

	/// <summary>Text inside an element.</summary>
	public unsafe class UITextComponent : Component
	{
		/// <summary>The text; <c>\n</c> starts a new line. For a text input this is what the player typed.</summary>
		public string Text
		{
			get => InternalCalls.UI_GetText(Entity.ID, (int)UITextField.Text);
			set => InternalCalls.UI_SetText(Entity.ID, (int)UITextField.Text, value);
		}

		/// <summary>Color (RGBA, 0 to 1).</summary>
		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.UI_GetVector(Entity.ID, (int)UIVectorField.TextColor, &c); return c; }
			set { InternalCalls.UI_SetVector(Entity.ID, (int)UIVectorField.TextColor, &value); }
		}

		/// <summary>The height of a line in UI units.</summary>
		public float Size
		{
			get => InternalCalls.UI_GetFloat(Entity.ID, (int)UIFloatField.TextSize);
			set => InternalCalls.UI_SetFloat(Entity.ID, (int)UIFloatField.TextSize, value);
		}
	}

	/// <summary>
	/// Behaviour of an element: a button, toggle, slider, progress bar, text input or scroll list. Subscribe to the events
	/// to react to the player; handlers are dropped when the scene ends. Setting a value from a script raises no event.
	/// </summary>
	public unsafe class UIWidgetComponent : Component
	{
		/// <summary>Whether the widget reacts to the pointer, keyboard and gamepad. A disabled one is drawn dimmed.</summary>
		public bool Interactable
		{
			get => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.WidgetInteractable);
			set => InternalCalls.UI_SetBool(Entity.ID, (int)UIBoolField.WidgetInteractable, value);
		}

		/// <summary>A slider's or progress bar's value, between <see cref="MinValue"/> and <see cref="MaxValue"/>.</summary>
		public float Value
		{
			get => InternalCalls.UI_GetFloat(Entity.ID, (int)UIFloatField.WidgetValue);
			set => InternalCalls.UI_SetFloat(Entity.ID, (int)UIFloatField.WidgetValue, value);
		}

		/// <summary>The value at the left end of a slider or progress bar.</summary>
		public float MinValue
		{
			get => InternalCalls.UI_GetFloat(Entity.ID, (int)UIFloatField.WidgetMin);
			set => InternalCalls.UI_SetFloat(Entity.ID, (int)UIFloatField.WidgetMin, value);
		}

		/// <summary>The value at the right end of a slider or progress bar.</summary>
		public float MaxValue
		{
			get => InternalCalls.UI_GetFloat(Entity.ID, (int)UIFloatField.WidgetMax);
			set => InternalCalls.UI_SetFloat(Entity.ID, (int)UIFloatField.WidgetMax, value);
		}

		/// <summary>Whether a slider moves in whole steps.</summary>
		public bool WholeNumbers
		{
			get => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.WidgetWholeNumbers);
			set => InternalCalls.UI_SetBool(Entity.ID, (int)UIBoolField.WidgetWholeNumbers, value);
		}

		/// <summary>A toggle's state.</summary>
		public bool Checked
		{
			get => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.WidgetChecked);
			set => InternalCalls.UI_SetBool(Entity.ID, (int)UIBoolField.WidgetChecked, value);
		}

		/// <summary>The text a text input shows, in half strength, while it is empty and not being edited.</summary>
		public string Placeholder
		{
			get => InternalCalls.UI_GetText(Entity.ID, (int)UITextField.Placeholder);
			set => InternalCalls.UI_SetText(Entity.ID, (int)UITextField.Placeholder, value);
		}

		/// <summary>How far a scroll list's content is moved up, in UI units.</summary>
		public float ScrollOffset
		{
			get => InternalCalls.UI_GetFloat(Entity.ID, (int)UIFloatField.ScrollOffset);
			set => InternalCalls.UI_SetFloat(Entity.ID, (int)UIFloatField.ScrollOffset, value);
		}

		/// <summary>True while the pointer is over the widget (and no widget in front of it is).</summary>
		public bool IsHovered => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.WidgetHovered);

		/// <summary>True while the primary button is held down on the widget.</summary>
		public bool IsPressed => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.WidgetPressed);

		/// <summary>True while the widget has the keyboard and gamepad focus.</summary>
		public bool IsFocused => InternalCalls.UI_GetBool(Entity.ID, (int)UIBoolField.WidgetFocused);

		/// <summary>Gives the widget the focus, as if the player had navigated to it. Use it to pick the first button of a menu.</summary>
		public void Focus() => InternalCalls.UI_Focus(Entity.ID);

		/// <summary>
		/// Clicks the widget as the player would: a button raises <see cref="Clicked"/>, a toggle flips and raises
		/// <see cref="Toggled"/>. The handlers run at the start of the next frame. Does nothing to a disabled widget.
		/// </summary>
		public void Click() => InternalCalls.UI_Click(Entity.ID);

		/// <summary>A button was clicked (pressed and released on it, or confirmed with the keyboard or gamepad).</summary>
		public event Action Clicked
		{
			add => UIEvents.For(Entity.ID).Clicked += value;
			remove => UIEvents.For(Entity.ID).Clicked -= value;
		}

		/// <summary>A toggle was flipped by the player; the argument is its new state.</summary>
		public event Action<bool> Toggled
		{
			add => UIEvents.For(Entity.ID).Toggled += value;
			remove => UIEvents.For(Entity.ID).Toggled -= value;
		}

		/// <summary>A slider was moved by the player; the argument is its new value.</summary>
		public event Action<float> ValueChanged
		{
			add => UIEvents.For(Entity.ID).ValueChanged += value;
			remove => UIEvents.For(Entity.ID).ValueChanged -= value;
		}

		/// <summary>The player changed a text input's text; the argument is the whole text.</summary>
		public event Action<string> TextChanged
		{
			add => UIEvents.For(Entity.ID).TextChanged += value;
			remove => UIEvents.For(Entity.ID).TextChanged -= value;
		}

		/// <summary>The player pressed Enter in a text input; the argument is its text.</summary>
		public event Action<string> Submitted
		{
			add => UIEvents.For(Entity.ID).Submitted += value;
			remove => UIEvents.For(Entity.ID).Submitted -= value;
		}

		/// <summary>The player pressed Cancel while this widget was focused (not while editing a text input).</summary>
		public event Action Cancelled
		{
			add => UIEvents.For(Entity.ID).Cancelled += value;
			remove => UIEvents.For(Entity.ID).Cancelled -= value;
		}
	}
}
