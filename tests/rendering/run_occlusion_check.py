#!/usr/bin/env python3
"""Occlusion culling must not change the picture (issue: popping in the Coin Run sample).

Captures one scene of a project twice with the Release editor, occlusion culling on and off, and requires the two images to match
(golden_compare). The default is Coin Run's level from the editor's sample set. The cause it covers: the culling shaders tested
against the closest-depth HZB (built for SSR) instead of a farthest-depth one, so a thin nearby occluder culled objects it only
partly covered. Windows and Linux; needs a built Release editor and a GPU with a desktop session.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "bin/Release-windows-x86_64/Editor/Editor.exe" if os.name == "nt" else ROOT / "bin/release-linux-x86_64/Editor/Editor"


def capture(project, scene, name, options, out, user):
    env = dict(os.environ)
    env.update({
        "LUX_USER_DATA_DIR": str(user), "LUX_NO_WATCHDOG": "1", "LUX_SKIP_BUILD": "1",
        "LUX_GOLDEN_DIR": str(out), "LUX_GOLDEN_SCENE": scene, "LUX_GOLDEN_NAME": name,
        "LUX_GOLDEN_PROJECT": str(project), "LUX_GOLDEN_SIZE": "1280x720",
        "LUX_GOLDEN_FRAME": "120", "LUX_GOLDEN_PERF_FRAMES": "1", "LUX_GOLDEN_PERF_WARMUP": "0",
        "LUX_GOLDEN_OPTIONS": options, "LUX_GOLDEN_SELFTEST": "0", "LUX_GOLDEN_PLAY": "0",
    })
    subprocess.run([str(EDITOR)], cwd=EDITOR.parent.parent.parent.parent / "Editor", env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=600)
    status = out / f"{name}.status"
    if not (status.is_file() and status.read_text(encoding="utf-8").startswith("OK")):
        raise RuntimeError(f"capture {name} failed; see {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", default=str(ROOT / "Editor/Samples/CoinRun/CoinRun.luxproj"))
    parser.add_argument("--scene", default="Scenes/Level.luxscene")
    parser.add_argument("--max-bad-percent", default="0.0")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="lux-occlusion-") as tmp:
        root = Path(tmp)
        user = root / "user"
        user.mkdir()
        (user / "UserPreferences.yaml").write_text("UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n", encoding="utf-8")
        for name, options in (("occluded", "OcclusionCulling=1"), ("reference", "OcclusionCulling=0")):
            capture(args.project, args.scene, name, options, root, user)
        result = subprocess.run([sys.executable, str(ROOT / "tests/rendering/golden_compare.py"), str(root / "occluded.lximg"), str(root / "reference.lximg"),
                                 "--max-bad-percent", args.max_bad_percent])
        if result.returncode:
            print("FAIL: occlusion culling changed the picture")
            return 1
    print("PASS: occlusion culling on and off render the same picture")
    return 0


if __name__ == "__main__":
    sys.exit(main())
