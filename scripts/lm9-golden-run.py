#!/usr/bin/env python3
"""Compatibility entry point for the acoustic plan's golden-run command."""

from pathlib import Path
import runpy


if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parents[1] / "tests/rendering/golden_run.py"), run_name="__main__")
