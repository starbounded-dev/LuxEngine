namespace Lux
{
	public enum MouseButton : ushort
	{
		Left = 0,
		Right = 1,
		Middle = 2
	}

	public class Input
	{
		public static unsafe bool IsKeyDown(KeyCode keycode)
		{
			return InternalCalls.Input_IsKeyDown(keycode);
		}

		public static unsafe bool IsMouseButtonDown(MouseButton button)
		{
			return InternalCalls.Input_IsMouseButtonDown(button);
		}
	}
}
