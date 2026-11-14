#!/usr/bin/env python3
"""Verify warm-object reuse and per-build-version invalidation with a real C++ build."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("persistent", ROOT / "scripts/ci/PersistentBuild.py")
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)
platform = "windows" if os.name == "nt" else "linux"


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)


# MSBuild excludes the operating-system TEMP tree from dependency tracking (MSB8029).
# Keep the fixture in a verified, ignored checkout directory like the real runner workspace.
bench_parent = ROOT / "bin/ci-cache-checks"
if not bench_parent.resolve().is_relative_to(ROOT.resolve()):
    raise RuntimeError("Refusing redirected benchmark directory")
bench_parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="build-", dir=bench_parent) as temp:
    root = Path(temp)
    config = "Release"
    include = root / "include/Lux/Core"
    include.mkdir(parents=True)
    shutil.copyfile(ROOT / "Core/Source/Lux/Core/Version.h", include / "Version.h")
    version = include / "BuildVersion.h"
    version.write_text('#define LUX_VERSION "test-first"\n')
    (root / "stable.h").write_text("#pragma once\n#include <cstdint>\n")
    (root / "stable.cpp").write_text('#include "stable.h"\nstd::uint64_t stable() { return 1; }\n')
    (root / "version.cpp").write_text('#include <Lux/Core/Version.h>\nconst char* version() { return LUX_VERSION_LONG; }\n')
    output = f"bin/{config}-{platform}-x86_64/Core"
    (root / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.20)
project(IncrementalCheck LANGUAGES CXX)
add_library(Core STATIC stable.cpp version.cpp)
target_compile_features(Core PRIVATE cxx_std_20)
target_compile_definitions(Core PRIVATE LUX_RELEASE LUX_PLATFORM_{platform.upper()})
target_include_directories(Core PRIVATE include)
target_precompile_headers(Core PRIVATE stable.h)
set_target_properties(Core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${{CMAKE_SOURCE_DIR}}/{output}" ARCHIVE_OUTPUT_DIRECTORY "${{CMAKE_SOURCE_DIR}}/{output}")
""")
    build = root / f"bin-int/{config}-{platform}-x86_64/cmake"
    metadata = root / ".git/cache.json"
    cache.prepare(root, platform, config, "fixture-toolchain-v1", metadata)
    start = time.monotonic()
    run("cmake", "-S", str(root), "-B", str(build))
    run("cmake", "--build", str(build), "--config", config)
    cold = time.monotonic() - start
    stable = next(p for p in build.rglob("*") if p.name in ("stable.cpp.o", "stable.obj"))
    compiled_version = next(p for p in build.rglob("*") if p.name in ("version.cpp.o", "version.obj"))
    stamp = stable.stat().st_mtime_ns
    version_stamp = compiled_version.stat().st_mtime_ns
    archive = root / output / ("Core.lib" if platform == "windows" else "libCore.a")
    archive_stamp = archive.stat().st_mtime_ns
    start = time.monotonic()
    cache.prepare(root, platform, config, "fixture-toolchain-v1", metadata)
    run("cmake", "--build", str(build), "--config", config)
    warm = time.monotonic() - start
    assert stable.stat().st_mtime_ns == stamp, "unchanged object was recompiled"
    assert archive.stat().st_mtime_ns > archive_stamp, "final archive was not freshly generated"
    time.sleep(1.1)
    version.write_text('#define LUX_VERSION "test-second"\n')
    run("cmake", "--build", str(build), "--config", config)
    assert stable.stat().st_mtime_ns == stamp, "version metadata invalidated stable PCH/object"
    assert compiled_version.stat().st_mtime_ns > version_stamp, "version consumer did not rebuild"
    time.sleep(1.1)
    (root / "stable.cpp").write_text('#include "stable.h"\nstd::uint64_t stable() { return 2; }\n')
    run("cmake", "--build", str(build), "--config", config)
    assert stable.stat().st_mtime_ns > stamp, "changed implementation did not rebuild"
    print(json.dumps(dict(platform=platform, cold_seconds=round(cold, 3), warm_seconds=round(warm, 3),
                          unchanged_objects_reused=True, version_change_rebuilt_consumer_only=True,
                          source_change_rebuilt_object=True)))
