#!/usr/bin/env python3
"""Regression checks for launching and validating exported acoustic probes."""

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import yaml

import run_scene_acoustics as runner


class ExportVerificationTests(unittest.TestCase):
    def verify_package(self, gpu_milliseconds):
        case = runner.CASES["gpu-reflection"]
        source = {
            "UUID": runner.SOURCE_ID, "Listener": case["listener"], "Source": case["source"],
            "DirectGainLFBits": 1065353216, "EngineRayGainLFBits": 1065353216,
            "DirectGainLF": 1, "EngineRayGainLF": 1, "RawGainLF": 1, "OcclusionGainLF": 1,
            "OpeningPaths": 0, "ReflectionPaths": 1, "WorkLimited": False,
            "AddedFocusEnergy": 0, "FocusedReflectionSend": 0,
        }
        report = {
            "EngineRayComparison": True, "ReverbOn": "GPU", "ReverbMeasurements": 1,
            "SourceSolveOn": "GPU", "SourceGpuTimingEnabled": True,
            "SourceGpuMilliseconds": gpu_milliseconds, "SourceVisibilityBatches": 4,
            "SourceVisibilityGpuFailed": False, "ReverbSend": 0.1, "ReverbDecaySeconds": 0.5,
            "StaticTriangles": 2, "MovingTriangles": 0, "StaticMeshInstances": 1,
            "MovingMeshInstances": 0, "CheckedSources": 1, "Sources": [source],
        }
        with tempfile.TemporaryDirectory(prefix="soundwave-export-test-") as temporary:
            root = Path(temporary).resolve()
            capture, package = root / "capture", root / "export" / "package"
            capture.mkdir()
            (package / "Data/Assets").mkdir(parents=True)
            executable = package / "Lux-Runtime"
            for artifact in (executable, package / "Data/Assets/Project.luxruntime",
                             package / "Data/Assets/AssetPack.lap", package / "Data/Assets/ShaderPack.lsp"):
                artifact.write_bytes(b"fixture")
            (capture / "AudioDemo.export.yaml").write_text(yaml.safe_dump({
                "Root": str(package), "Executable": str(executable),
            }), encoding="utf-8")
            (capture / "AudioDemo.audio.yaml").write_text(yaml.safe_dump(report), encoding="utf-8")
            report_path = capture / "AudioDemo.runtime-audio.yaml"

            def run_runtime(command, **kwargs):
                self.assertFalse(report_path.exists())
                self.assertEqual(kwargs["env"]["LUX_VERIFY_GPU_SOURCE"], "1")
                self.assertEqual(kwargs["cwd"], package)
                report_path.write_text(yaml.safe_dump(report), encoding="utf-8")
                Path(str(report_path) + ".status").write_text("OK\n", encoding="utf-8")
                return subprocess.CompletedProcess(command, 0)

            with patch.object(runner.subprocess, "run", side_effect=run_runtime):
                return runner.verify_export(capture, root / "export", case, {"Default": 2},
                                            10, "single", "release", gpu_source=True)

    def test_gpu_export_reads_the_report_after_runtime_finishes(self):
        result = self.verify_package(1.0)
        self.assertEqual(result["values"]["ReflectionPaths"], 1)

    def test_gpu_export_still_rejects_missing_timestamp_evidence(self):
        with self.assertRaisesRegex(RuntimeError, "GPU timestamp evidence missing"):
            self.verify_package(0.0)


if __name__ == "__main__":
    unittest.main()
