namespace Lux
{
	// ushort-backed to match the native MouseButton (enum class MouseButton : uint16_t).
	public enum MouseButton : ushort
	{
		Left = 0,
		Right = 1,
		Middle = 2
	}

	public enum CursorMode
	{
		Normal = 0,
		Hidden = 1,
		Locked = 2
	}

	public static unsafe class Input
	{
		public static bool IsKeyDown(KeyCode keycode) => InternalCalls.Input_IsKeyDown(keycode);
		public static bool IsKeyPressed(KeyCode keycode) => InternalCalls.Input_IsKeyPressed(keycode);
		public static bool IsKeyHeld(KeyCode keycode) => InternalCalls.Input_IsKeyHeld(keycode);
		public static bool IsKeyReleased(KeyCode keycode) => InternalCalls.Input_IsKeyReleased(keycode);

		public static bool IsMouseButtonDown(MouseButton button) => InternalCalls.Input_IsMouseButtonDown(button);
		public static bool IsMouseButtonPressed(MouseButton button) => InternalCalls.Input_IsMouseButtonPressed(button);
		public static bool IsMouseButtonHeld(MouseButton button) => InternalCalls.Input_IsMouseButtonHeld(button);
		public static bool IsMouseButtonReleased(MouseButton button) => InternalCalls.Input_IsMouseButtonReleased(button);

		public static float MouseX => InternalCalls.Input_GetMouseX();
		public static float MouseY => InternalCalls.Input_GetMouseY();

		public static Vector2 MousePosition
		{
			get
			{
				Vector2 position;
				InternalCalls.Input_GetMousePosition(&position);
				return position;
			}
			set => InternalCalls.Input_SetMousePosition(value.X, value.Y);
		}

		public static void SetCursorMode(CursorMode mode) => InternalCalls.Input_SetCursorMode(mode);

		public static bool IsControllerPresent(int id) => InternalCalls.Input_IsControllerPresent(id);
		public static bool IsControllerButtonDown(int id, int button) => InternalCalls.Input_IsControllerButtonDown(id, button);
		public static float GetControllerAxis(int id, int axis) => InternalCalls.Input_GetControllerAxis(id, axis);
	}
}
