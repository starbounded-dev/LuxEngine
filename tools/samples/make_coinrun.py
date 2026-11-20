"""Builds Editor/Samples/CoinRun, the small 3D sample game (docs/SAMPLE_GAMES_PLAN.md). Run from the repository root.

A third-person character on a course of platforms: ten coins, a goal that opens once they are all collected, particles, and a small UI. The
scripts in Assets/Scripts/Source are written by hand and kept; this file writes the project, the materials and the scene.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import luxauthor as lux  # noqa: E402

NAMESPACE = "CoinRun"
FOLDER = lux.EDITOR / "Samples" / "CoinRun"
SCRIPTS = ["Game.cs", "PlayerController.cs", "FollowCamera.cs", "Coin.cs", "Goal.cs", "GameManager.cs"]

# Platforms: (name, centre x, top y, centre z, size x, size z). Each is a metre thick, hanging below its top.
PLATFORMS = [
    ("Start", 0.0, 0.0, 0.0, 10.0, 10.0),
    ("Step1", 0.0, 0.5, -10.0, 5.0, 5.0),
    ("Step2", 3.5, 1.2, -16.0, 4.0, 4.0),
    ("Step3", 7.0, 1.9, -21.5, 4.0, 4.0),
    ("Step4", 3.0, 2.6, -27.0, 4.0, 4.0),
    ("Summit", 0.0, 3.3, -35.0, 8.0, 8.0),
]
# Coins, in the order a player (or the sample's autopilot) takes them: x, y (centre), z.
COINS = [
    (-3.0, 1.0, -2.0), (3.0, 1.0, 2.0),
    (-1.0, 1.5, -10.0), (1.0, 1.5, -10.5),
    (3.5, 2.2, -16.0), (7.0, 2.9, -21.5), (3.0, 3.6, -27.0),
    (-2.0, 4.3, -35.0), (2.0, 4.3, -35.0), (0.0, 4.3, -33.0),
]
GOAL = (0.0, 4.8, -37.0)
SPAWN = (0.0, 1.0, 2.0)


def main():
    project = lux.Project(FOLDER, "CoinRun", NAMESPACE)
    for mesh in ("Cube", "Sphere", "Cylinder", "Capsule"):
        project.default_mesh(mesh)

    grass = project.material("Grass", (0.30, 0.62, 0.28), roughness=0.9)
    stone = project.material("Stone", (0.62, 0.60, 0.56), roughness=0.85)
    gold = project.material("Gold", (1.0, 0.72, 0.12), roughness=0.35, metalness=0.8, emission=1.2)
    hero = project.material("Hero", (0.22, 0.45, 0.85), roughness=0.55)
    goal_closed = project.material("GoalClosed", (0.35, 0.18, 0.5), roughness=0.5, emission=0.4)

    ids = lux.Ids(5100000000000000000)
    entities = []

    entities.append(lux.entity(ids(), "Sun", rot=(-0.9, 0.5, 0), body=lux.sun(6)))
    entities.append(lux.entity(ids(), "Sky", body=lux.sky(0.55)))

    for name, x, top, z, sx, sz in PLATFORMS:
        material = grass if name == "Start" or name == "Summit" else stone
        entities.append(lux.entity(ids(), name, pos=(x, top - 0.5, z), scale=(sx, 1, sz),
                                   body=lux.mesh_component("Cube", material) + lux.rigid("static") + lux.box_collider((0.5, 0.5, 0.5), 0.8)))

    # The player: the controller and its capsule on the root, the visible capsule as a child.
    player, body = ids(), ids()
    entities.append(lux.entity(player, "Player", pos=SPAWN, children=(body,), script="PlayerController", namespace=NAMESPACE,
                               body=lux.character_controller(0.4, 0.5)))
    entities.append(lux.entity(body, "PlayerBody", pos=(0, 0, 0), scale=(0.8, 0.9, 0.8), parent=player, body=lux.mesh_component("Capsule", hero)))

    entities.append(lux.entity(ids(), "Camera", pos=(0, 5, 9), rot=(-0.4, 0, 0), script="FollowCamera", namespace=NAMESPACE, body=lux.camera(60)))

    for index, (x, y, z) in enumerate(COINS, start=1):
        entities.append(lux.entity(ids(), f"Coin{index:02d}", pos=(x, y, z), scale=(0.7, 0.1, 0.7), rot=(1.5708, 0, 0), script="Coin", namespace=NAMESPACE,
                                   body=lux.mesh_component("Cylinder", gold) + lux.rigid("static", trigger=True) + lux.sphere_collider(0.8)))

    # The goal: a pillar; once every coin is in it pulses and sparks rise from it, and standing next to it wins (Goal.cs measures the distance).
    goal = ids()
    entities.append(lux.entity(goal, "Goal", pos=GOAL, scale=(1.2, 3.0, 1.2), script="Goal", namespace=NAMESPACE,
                               body=lux.mesh_component("Cylinder", goal_closed)))
    entities.append(lux.entity(ids(), "GoalSparks", pos=(GOAL[0], GOAL[1] + 1.6, GOAL[2]),
                               body=lux.particles(PlayOnAwake="false", Seed=7, Looping="true", Duration=2, Rate=40, MaxParticles=300,
                                                  Lifetime=(1.2, 2.0), Speed=(0.8, 1.6), Size=(0.08, 0.14), Shape=0, ConeAngle=20, Radius=0.5,
                                                  Gravity=(0, 0.4, 0), WorldSpace="true", StartColor=(6, 4, 1.2, 1),
                                                  ColorOverLife=[(0, 1, 1, 1, 1), (1, 1, 0.6, 0.2, 0)], SizeOverLife=[(0, 1), (1, 0.2)],
                                                  Blend=1, SoftDistance=0.1, RenderMode=0)))
    entities.append(lux.entity(ids(), "Sparkle", pos=(0, -50, 0),
                               body=lux.particles(PlayOnAwake="false", Seed=3, Looping="false", Duration=0.5, Rate=0, MaxParticles=200,
                                                  Lifetime=(0.4, 0.8), Speed=(2.0, 4.5), Size=(0.1, 0.18), Shape=1, Radius=0.2,
                                                  Gravity=(0, -6, 0), WorldSpace="true", StartColor=(8, 5, 1, 1),
                                                  ColorOverLife=[(0, 1, 1, 1, 1), (1, 1, 0.5, 0.1, 0)], SizeOverLife=[(0, 1), (1, 0.1)],
                                                  Blend=1, SoftDistance=0.05, RenderMode=0)))

    # The interface: coins and the clock at the top, a message in the middle, the controls at the bottom.
    canvas, coins, clock, message, help_text = ids(), ids(), ids(), ids(), ids()
    entities.append(lux.entity(canvas, "HUD", children=(coins, clock, message, help_text), body=lux.canvas()))
    entities.append(lux.entity(coins, "CoinsText", parent=canvas,
                               body=lux.ui_text("Coins 0 / 10", 56, (1, 0.85, 0.3, 1), (0, 1), (0, 1), (40, -110), (700, -30), 0, 1)))
    entities.append(lux.entity(clock, "TimeText", parent=canvas,
                               body=lux.ui_text("0.0 s", 48, (1, 1, 1, 1), (1, 1), (1, 1), (-500, -110), (-40, -30), 2, 1)))
    entities.append(lux.entity(message, "MessageText", parent=canvas,
                               body=lux.ui_text("", 96, (1, 1, 1, 1), (0, 0), (1, 1), (0, 0), (0, 0), 1, 1)))
    entities.append(lux.entity(help_text, "HelpText", parent=canvas,
                               body=lux.ui_text("WASD move   Space jump   Q / E turn the camera   R restart", 34, (1, 1, 1, 0.8), (0, 0), (1, 0), (0, 24), (0, 80), 1, 1)))

    entities.append(lux.entity(ids(), "GameManager", script="GameManager", namespace=NAMESPACE))

    scene = project.scene("Level", entities)
    project.script_project(SCRIPTS)
    project.finish(scene, extra_project="")
    print(f"CoinRun: {len(entities)} entities, {len(project.assets)} assets")


if __name__ == "__main__":
    main()
