# Sample Games

The release ships two small games in the `Samples` folder, made only with what the editor gives you. Open either with **File > Open Project** and press Play.

- **Coin Run** (3D, `Samples/CoinRun`): a third-person character on a course of platforms. Collect ten coins, then reach the glowing goal. WASD or the arrows move, Space jumps, Q and E turn the camera, R restarts. It uses a character controller, triggers, particles, a follow camera and the game UI.
- **Pixel Hop** (2D, `Samples/PixelHop`): a tilemap platformer in two levels. A / D move, Space jumps, stomp the pink blobs, reach the flag. It uses a tilemap with a sprite sheet, Box2D, sprite animation, an orthographic camera, scene loading and the game UI.

Their scripts are in each project's `Assets/Scripts/Source`. Read them, change them and press **Ctrl+R**, or use them as a starting point: the art in both games is drawn in code, so you may use it freely.

To see a game play itself, set the environment variable `LUX_SAMPLE_AUTOPLAY=1` before starting it: the character walks and jumps through the level and the game quits when it is done (this is how the samples are tested).
