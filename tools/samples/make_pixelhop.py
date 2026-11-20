"""Builds Editor/Samples/PixelHop, the small 2D sample game (docs/SAMPLE_GAMES_PLAN.md). Run from the repository root.

A tilemap platformer: a Box2D hero that runs and jumps, coins, patrolling enemies you can stomp, a flag, two levels, a small UI. The art is drawn
by this file (rectangles in a 16 x 16 pixel grid), so it is original and there is nothing to license. The scripts in Assets/Scripts/Source are
written by hand and kept.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import luxauthor as lux  # noqa: E402

NAMESPACE = "PixelHop"
FOLDER = lux.EDITOR / "Samples" / "PixelHop"
SCRIPTS = ["Game.cs", "Hero.cs", "CameraFollow.cs", "Coin.cs", "Enemy.cs", "Flag.cs", "GameManager.cs"]

# Atlas frames, one 16 x 16 cell each.
GROUND, BRICK, IDLE, RUN1, RUN2, ENEMY1, ENEMY2, COIN, FLAG = range(9)
FRAME_NAMES = ["ground", "brick", "idle", "run1", "run2", "enemy1", "enemy2", "coin", "flag"]

# Each level: width, gaps (x, length) in the ground, brick blocks (x, height), enemy x, coin (x, y) with y 2 or 3, the flag x. The hero starts at x = 1.
SPECS = {
    "Level1": dict(width=66, gaps=[(14, 2), (30, 2), (44, 3)], blocks=[(9, 1), (22, 1), (37, 2), (51, 1)], enemies=[18, 27, 40, 49],
                   coins=[(5, 2), (7, 3), (11, 3), (16, 3), (20, 2), (25, 3), (33, 3), (35, 2), (42, 3), (47, 2), (54, 2)], flag=56),
    "Level2": dict(width=72, gaps=[(10, 2), (20, 3), (31, 3), (46, 2), (55, 3)], blocks=[(6, 1), (15, 2), (26, 1), (38, 2), (42, 1), (51, 1)], enemies=[13, 23, 34, 40, 49, 58],
                   coins=[(4, 2), (8, 3), (12, 3), (17, 3), (24, 3), (28, 2), (36, 3), (44, 3), (48, 2), (53, 3), (60, 2)], flag=62),
}
HEIGHT = 8


def build_rows(spec):
    """The level as rows from the top: # ground, B brick, c coin, e enemy, P hero start, F flag."""
    width = spec["width"]
    grid = [["."] * width for _ in range(HEIGHT)]  # grid[y][x], y up
    for x in range(width):
        if not any(gx <= x < gx + length for gx, length in spec["gaps"]):
            grid[0][x] = grid[1][x] = "#"
    for x, height in spec["blocks"]:
        for y in range(2, 2 + height):
            grid[y][x] = "B"
    for x in spec["enemies"]:
        grid[2][x] = "e"
    for x, y in spec["coins"]:
        grid[y][x] = "c"
    grid[2][1] = "P"
    grid[2][spec["flag"]] = "F"
    return ["".join(grid[y]) for y in range(HEIGHT - 1, -1, -1)]


LEVELS = {name: build_rows(spec) for name, spec in SPECS.items()}


def draw_atlas():
    def blank():
        return [[(0, 0, 0, 0)] * 16 for _ in range(16)]

    def fill(cell, x0, y0, x1, y1, color):
        for y in range(y0, y1):
            for x in range(x0, x1):
                cell[y][x] = color

    cells = []
    ground = blank()
    fill(ground, 0, 0, 16, 16, (110, 72, 44, 255))
    fill(ground, 0, 0, 16, 4, (86, 176, 70, 255))
    fill(ground, 0, 4, 16, 5, (60, 130, 50, 255))
    for x, y in ((3, 8), (9, 11), (13, 7), (6, 13)):
        fill(ground, x, y, x + 2, y + 1, (90, 58, 34, 255))
    cells.append(ground)
    brick = blank()
    fill(brick, 0, 0, 16, 16, (186, 96, 64, 255))
    for y in (0, 5, 10):
        fill(brick, 0, y, 16, y + 1, (120, 60, 44, 255))
    for x, y in ((7, 1), (3, 6), (11, 6), (7, 11)):
        fill(brick, x, y, x + 1, y + 4, (120, 60, 44, 255))
    cells.append(brick)

    def hero(leg):
        cell = blank()
        fill(cell, 4, 1, 12, 8, (60, 110, 220, 255))       # body
        fill(cell, 4, 8, 12, 14, (250, 214, 170, 255))     # head
        fill(cell, 4, 12, 12, 15, (180, 50, 50, 255))      # cap
        fill(cell, 9, 9, 11, 11, (30, 30, 40, 255))        # eye
        fill(cell, 4, 0, 7, 2 + leg, (40, 40, 90, 255))    # legs
        fill(cell, 9, 0, 12, 2 + (1 - leg), (40, 40, 90, 255))
        return cell
    cells.extend([hero(1), hero(0), hero(1)])

    def enemy(squash):
        cell = blank()
        fill(cell, 2, 0, 14, 8 - squash, (200, 70, 160, 255))
        fill(cell, 4, 4, 7, 7 - squash, (255, 255, 255, 255))
        fill(cell, 9, 4, 12, 7 - squash, (255, 255, 255, 255))
        fill(cell, 5, 4, 6, 6 - squash, (20, 20, 20, 255))
        fill(cell, 10, 4, 11, 6 - squash, (20, 20, 20, 255))
        return cell
    cells.extend([enemy(0), enemy(1)])
    coin = blank()
    fill(coin, 4, 3, 12, 13, (250, 200, 40, 255))
    fill(coin, 3, 5, 13, 11, (250, 200, 40, 255))
    fill(coin, 6, 5, 10, 11, (255, 236, 130, 255))
    cells.append(coin)
    flag = blank()
    fill(flag, 7, 0, 9, 16, (230, 230, 230, 255))
    fill(flag, 9, 8, 16, 15, (80, 200, 120, 255))
    cells.append(flag)
    # The atlas is a single row; row 0 of the image is the top.
    return [sum((cell[row] for cell in cells), []) for row in range(15, -1, -1)]


def main():
    project = lux.Project(FOLDER, "PixelHop", NAMESPACE)
    rows = draw_atlas()
    lux.write_png(project.assets_dir / "Sprites" / "Atlas.png", 16 * len(FRAME_NAMES), 16, rows)
    project.add_binary_asset("Sprites/Atlas.png", "Texture")
    texture = lux.handle_for("Sprites/Atlas.png")
    frames = "".join(f"    - Name: {name}\n      Rect: [{index * 16}, 0, 16, 16]\n      Pivot: [0.5, 0.5]\n" + ("      Solid: true\n" if index in (GROUND, BRICK) else "")
                     for index, name in enumerate(FRAME_NAMES))
    clips = ("  Clips:\n    - Name: idle\n      FramesPerSecond: 4\n      Wrap: Loop\n      Frames: [2]\n"
             "    - Name: run\n      FramesPerSecond: 10\n      Wrap: Loop\n      Frames: [3, 4]\n"
             "    - Name: walk\n      FramesPerSecond: 4\n      Wrap: Loop\n      Frames: [5, 6]\n"
             "    - Name: still\n      FramesPerSecond: 4\n      Wrap: Loop\n      Frames: [7]\n")
    project.write_asset("Sprites/Atlas.lsheet", f"SpriteSheet:\n  Version: 1\n  Texture: {texture}\n  PixelsPerUnit: 16\n  CellSize: [16, 16]\n  Spacing: [0, 0]\n  Margin: [0, 0]\n"
                        f"  Frames:\n{frames}{clips}", "SpriteSheet")

    ids = lux.Ids(5200000000000000000)
    level_handles = {name: lux.handle_for(f"Scenes/{name}.luxscene") for name in LEVELS}
    order = list(LEVELS)
    scenes = []
    for number, (name, rows_text) in enumerate(LEVELS.items(), start=1):
        height = len(rows_text)
        width = len(rows_text[0])
        entities = []
        # The tilemap: ground and bricks, as runs along x from the bottom row up.
        runs, coins, enemies, start, flag = [], [], [], (1.5, 2.5), None
        for row_index, text in enumerate(rows_text):
            y = height - 1 - row_index
            x = 0
            while x < width:
                ch = text[x]
                if ch in "#B":
                    end = x
                    while end < width and text[end] in "#B":
                        end += 1
                    runs.append(f"          - [{y}, {x}, " + ", ".join(str(GROUND if c == "#" else BRICK) for c in text[x:end]) + "]\n")
                    x = end
                    continue
                if ch == "c":
                    coins.append((x + 0.5, y + 0.5))
                elif ch == "e":
                    enemies.append((x + 0.5, y + 0.5))
                elif ch == "P":
                    start = (x + 0.5, y + 0.5)
                elif ch == "F":
                    flag = (x + 0.5, y + 0.5)
                x += 1
        entities.append(lux.entity(ids(), "Tilemap", body=("    TilemapComponent:\n      TileSet: Sprites/Atlas.lsheet\n      CellSize: [1, 1]\n      PointFilter: true\n"
                                                           "      Friction: 0.4\n      Layers:\n        - Name: Ground\n          Runs:\n" + "".join(runs)
                                                           + "    RigidBody2DComponent:\n      BodyType: 0\n")))
        sprite = lambda frame: f"    SpriteRendererComponent:\n      Color: [1, 1, 1, 1]\n      Sheet: Sprites/Atlas.lsheet\n      SpriteIndex: {frame}\n      PointFilter: true\n      OrderInLayer: 5\n"
        entities.append(lux.entity(ids(), "Hero", pos=(start[0], start[1], 0), scale=(1, 1, 1), script="Hero", namespace=NAMESPACE,
                                   body=sprite(IDLE) + "    SpriteAnimatorComponent:\n      Clip: idle\n      Playing: true\n      Speed: 1\n"
                                   + "    RigidBody2DComponent:\n      BodyType: 1\n      FixedRotation: true\n      Mass: 1\n      GravityScale: 2.5\n"
                                   + "    BoxCollider2DComponent:\n      Offset: [0, -0.05]\n      Size: [0.32, 0.4]\n      Density: 1\n      Friction: 0\n"))
        for index, (x, y) in enumerate(coins, start=1):
            entities.append(lux.entity(ids(), f"Coin{index:02d}", pos=(x, y, 0), script="Coin", namespace=NAMESPACE,
                                       body=sprite(COIN) + "    RigidBody2DComponent:\n      BodyType: 0\n    BoxCollider2DComponent:\n      Size: [0.35, 0.35]\n      IsTrigger: true\n"))
        for index, (x, y) in enumerate(enemies, start=1):
            entities.append(lux.entity(ids(), f"Enemy{index:02d}", pos=(x, y, 0), script="Enemy", namespace=NAMESPACE,
                                       body=sprite(ENEMY1) + "    SpriteAnimatorComponent:\n      Clip: walk\n      Playing: true\n      Speed: 1\n"
                                       + "    RigidBody2DComponent:\n      BodyType: 1\n      FixedRotation: true\n      GravityScale: 2.5\n"
                                       + "    BoxCollider2DComponent:\n      Offset: [0, -0.1]\n      Size: [0.38, 0.3]\n      Friction: 0\n"))
        next_fields = ""
        if number < len(order):
            handle = level_handles[order[number]]
            next_fields = (f"      StoredFields:\n        - ID: {lux.fnv1a(f'{NAMESPACE}.Flag.Next')}\n          Name: Next\n          Type: Scene\n          Data: {handle}\n")
        entities.append(lux.entity(ids(), "Flag", pos=(flag[0], flag[1], 0), script="Flag", namespace=NAMESPACE, fields=next_fields, body=sprite(FLAG)))

        entities.append(lux.entity(ids(), "Camera", pos=(start[0], 4.5, 10), script="CameraFollow", namespace=NAMESPACE,
                                   body=("    CameraComponent:\n      Camera:\n        ProjectionType: 1\n        PerspectiveFOV: 45\n        PerspectiveNear: 0.1\n        PerspectiveFar: 100\n"
                                         "        OrthographicSize: 6\n        OrthographicNear: -1\n        OrthographicFar: 100\n      Primary: true\n      FixedAspectRatio: false\n"
                                         "      SolidBackground: true\n      BackgroundColor: [0.45, 0.72, 0.95, 1]\n")))
        canvas, coins_text, level_text, message, help_text = ids(), ids(), ids(), ids(), ids()
        entities.append(lux.entity(canvas, "HUD", children=(coins_text, level_text, message, help_text), body=lux.canvas()))
        entities.append(lux.entity(coins_text, "CoinsText", parent=canvas, body=lux.ui_text("Coins 0", 56, (1, 0.9, 0.3, 1), (0, 1), (0, 1), (40, -110), (600, -30), 0, 1)))
        entities.append(lux.entity(level_text, "LevelText", parent=canvas, body=lux.ui_text(f"Level {number} of {len(order)}", 48, (1, 1, 1, 1), (1, 1), (1, 1), (-600, -110), (-40, -30), 2, 1)))
        entities.append(lux.entity(message, "MessageText", parent=canvas, body=lux.ui_text("", 96, (1, 1, 1, 1), (0, 0), (1, 1), (0, 0), (0, 0), 1, 1)))
        entities.append(lux.entity(help_text, "HelpText", parent=canvas, body=lux.ui_text("A / D move   Space jump (stomp the pink blobs)   R restart", 34, (1, 1, 1, 0.9), (0, 0), (1, 0), (0, 24), (0, 80), 1, 1)))
        entities.append(lux.entity(ids(), "GameManager", script="GameManager", namespace=NAMESPACE))
        scenes.append(project.scene(name, entities))

    project.script_project(SCRIPTS)
    project.finish(scenes[0], window=(1280, 720))
    print(f"PixelHop: {len(LEVELS)} levels, {len(project.assets)} assets")


if __name__ == "__main__":
    main()
