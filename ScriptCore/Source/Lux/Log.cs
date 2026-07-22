namespace Lux
{
	// Managed logging that routes to the engine console via the NativeLog internal call.
	public static class Log
	{
		public static unsafe void Trace(string message) => InternalCalls.NativeLog(message, 0);
		public static unsafe void Info(string message) => InternalCalls.NativeLog(message, 1);
		public static unsafe void Warn(string message) => InternalCalls.NativeLog(message, 2);
		public static unsafe void Error(string message) => InternalCalls.NativeLog(message, 3);
	}
}
