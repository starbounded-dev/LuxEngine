// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>
	/// The running game: quitting it, pausing it, and being told when the window gains or loses focus, the view is resized,
	/// or the game ends. Handlers added to its events stay until removed, so remove them in <c>OnDestroy</c>; they are all
	/// dropped when the game stops.
	/// </summary>
	public static unsafe class Application
	{
		private static Action<bool>? s_FocusChanged;
		private static Action<bool>? s_PauseChanged;
		private static Action<int, int>? s_Resized;
		private static Action? s_Quitting;

		/// <summary>
		/// Ends the game after the current frame, as closing its window does. In the editor it does nothing but
		/// log a warning: stop Play there instead.
		/// </summary>
		public static void Quit() => InternalCalls.Application_Quit();

		/// <summary>Whether the game's window (or, in the editor, any editor window) has the keyboard focus and is not minimized.</summary>
		public static bool IsFocused => InternalCalls.Application_IsFocused();

		/// <summary>
		/// Whether the game is paused: scripts get no <c>OnUpdate</c> or <c>OnFixedUpdate</c>, physics and animation stand still.
		/// Timers and coroutines keep counting real time (<c>Timer.After(..., unscaled: true)</c>, <see cref="WaitForSecondsRealtime"/>),
		/// which is how a paused game gets unpaused. The accessibility menu pauses it too; <see cref="PauseChanged"/> reports both.
		/// </summary>
		public static bool Paused
		{
			get => InternalCalls.Application_GetPaused();
			set => InternalCalls.Application_SetPaused(value);
		}

		/// <summary>The width in pixels of the game's view: the window in an exported game, the Game View in the editor.</summary>
		public static int Width => InternalCalls.Application_GetWidth();

		/// <summary>The height in pixels of the game's view: the window in an exported game, the Game View in the editor.</summary>
		public static int Height => InternalCalls.Application_GetHeight();

		/// <summary>Raised when the window gains or loses focus, with the new <see cref="IsFocused"/>. Checked once a frame.</summary>
		public static event Action<bool> FocusChanged
		{
			add => s_FocusChanged += value;
			remove => s_FocusChanged -= value;
		}

		/// <summary>Raised when the game is paused or unpaused, with the new <see cref="Paused"/>. Checked once a frame.</summary>
		public static event Action<bool> PauseChanged
		{
			add => s_PauseChanged += value;
			remove => s_PauseChanged -= value;
		}

		/// <summary>Raised when the game's view changes size, with the new <see cref="Width"/> and <see cref="Height"/>. Not raised for a size of 0 (minimized).</summary>
		public static event Action<int, int> Resized
		{
			add => s_Resized += value;
			remove => s_Resized -= value;
		}

		/// <summary>
		/// Raised once when the game ends: the window is closed or <see cref="Quit"/> is called, and in the editor when Play
		/// stops. Scripts still exist then; their <c>OnDestroy</c> follows. Not raised when a script loads another scene.
		/// </summary>
		public static event Action Quitting
		{
			add => s_Quitting += value;
			remove => s_Quitting -= value;
		}

		// Every subscriber gets its call, and one that throws is logged, never the engine's frame.
		private static void Raise<T>(Action<T>? handlers, T argument)
		{
			if (handlers == null)
				return;
			foreach (Delegate handler in handlers.GetInvocationList())
			{
				try { ((Action<T>)handler)(argument); }
				catch (Exception exception) { Log.Error($"An Application event handler threw: {exception}"); }
			}
		}

		internal static void DispatchFocus(int focused) => Raise(s_FocusChanged, focused != 0);
		internal static void DispatchPause(int paused) => Raise(s_PauseChanged, paused != 0);

		internal static void DispatchResize(int width, int height)
		{
			if (s_Resized == null)
				return;
			foreach (Delegate handler in s_Resized.GetInvocationList())
			{
				try { ((Action<int, int>)handler)(width, height); }
				catch (Exception exception) { Log.Error($"An Application.Resized handler threw: {exception}"); }
			}
		}

		internal static void DispatchQuitting()
		{
			if (s_Quitting == null)
				return;
			foreach (Delegate handler in s_Quitting.GetInvocationList())
			{
				try { ((Action)handler)(); }
				catch (Exception exception) { Log.Error($"An Application.Quitting handler threw: {exception}"); }
			}
		}

		internal static void ResetEvents()
		{
			s_FocusChanged = null;
			s_PauseChanged = null;
			s_Resized = null;
			s_Quitting = null;
		}
	}
}
