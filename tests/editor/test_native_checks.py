"""Regression for self-test PASS markers hiding native graphics failures."""
from pathlib import Path
import tempfile
import unittest

from native_checks import require_clean_graphics_log


class NativeLogChecks(unittest.TestCase):
    def test_pass_marker_does_not_accept_failed_swapchain(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "editor.log"
            log.write_text("PASS\n[Renderer] NRI failed to create a 1600x900 swap chain\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, r"editor\.log:2:.*NRI failed to create"):
                require_clean_graphics_log(log)

    def test_expected_authoring_failure_and_missing_timing_remain_testable(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "editor.log"
            log.write_text("[Editor] Could not save material: destination is unavailable\n"
                           "[Profiler] GPU timing unavailable\nPASS\n", encoding="utf-8")
            require_clean_graphics_log(log)


if __name__ == "__main__":
    unittest.main()
