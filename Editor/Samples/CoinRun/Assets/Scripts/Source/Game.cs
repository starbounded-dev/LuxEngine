// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace CoinRun
{
	// What the scripts share: the score, the clock and the end of a run. GameManager resets it whenever the level starts.
	public static class Game
	{
		public const int TotalCoins = 10;

		public static int Coins;
		public static float Clock;
		public static bool Won;
		public static float CameraYaw;

		// The sample plays itself when LUX_SAMPLE_AUTOPLAY=1 is set (the sample tests do): the character walks and jumps toward the coins
		// and the goal through the same movement code a player uses.
		public static bool Autoplay;

		public static void Reset()
		{
			Coins = 0;
			Clock = 0.0f;
			Won = false;
			Autoplay = System.Environment.GetEnvironmentVariable("LUX_SAMPLE_AUTOPLAY") == "1";
		}
	}
}
