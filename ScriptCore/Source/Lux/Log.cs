// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

namespace Lux
{
	/// <summary>Writes messages to the editor's Log panel and the log file.</summary>
	public static class Log
	{
		/// <summary>Logs a trace message, the lowest level.</summary>
		public static unsafe void Trace(string message) => InternalCalls.NativeLog(message, 0);
		/// <summary>Logs an informational message.</summary>
		public static unsafe void Info(string message) => InternalCalls.NativeLog(message, 1);
		/// <summary>Logs a warning.</summary>
		public static unsafe void Warn(string message) => InternalCalls.NativeLog(message, 2);
		/// <summary>Logs an error.</summary>
		public static unsafe void Error(string message) => InternalCalls.NativeLog(message, 3);
	}
}
