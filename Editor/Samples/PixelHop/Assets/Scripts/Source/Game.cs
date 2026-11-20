// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace PixelHop
{
	// What the scripts share: the coins taken, whether the level is over, and the sample's autopilot.
	public static class Game
	{
		public static int Coins;
		public static bool LevelDone;

		// Not reset by a restart: the autopilot's clock, so a run that keeps dying still ends.
		public static float AutoplayClock;

		// The sample plays itself when LUX_SAMPLE_AUTOPLAY=1 is set (the sample tests do): the hero runs right and jumps.
		public static bool Autoplay;

		public static void StartLevel()
		{
			LevelDone = false;
			Autoplay = System.Environment.GetEnvironmentVariable("LUX_SAMPLE_AUTOPLAY") == "1";
		}
	}
}
