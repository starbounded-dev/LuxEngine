// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

namespace Lux
{
	/// <summary>The running game.</summary>
	public static class Application
	{
		/// <summary>
		/// Ends the game after the current frame, as closing its window does. In the editor it does nothing but
		/// log a warning: stop Play there instead.
		/// </summary>
		public static unsafe void Quit() => InternalCalls.Application_Quit();
	}
}
