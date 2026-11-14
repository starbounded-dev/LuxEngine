"""Failure checks shared by native editor integration runners."""
from pathlib import Path


def require_clean_graphics_log(path: Path):
    """A self-test PASS marker cannot excuse failed native rendering or UI validation."""
    markers = (
        "NRI failed to create", "Resource is null!", "Bake - Validate failed",
        "VK_ERROR_SURFACE_LOST_KHR", "VUID-", "Validation Error",
        "Vulkan validation error", "Vulkan validation warning",
        "Assertion failed", "Verify Failed",
    )
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        if any(marker in line for marker in markers) or ("Shader '" in line and "failed to load" in line):
            raise RuntimeError(f"Native graphics or UI failure in {path}:{number}: {line.strip()}")
