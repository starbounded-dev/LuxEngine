#!/usr/bin/env python3
"""Linux headless audio regression checks: file and pack streams, Jolt contacts, Soundwave occlusion and music serialization.

Playback itself is tested by tests/audio/native (CMake, on every platform)."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent

def run(command, **kwargs):
    command = list(map(str, command))
    started = time.time_ns()
    subprocess.run(command, check=True, **kwargs)
    if "-o" in command:
        output = Path(command[command.index("-o") + 1])
        if not output.is_absolute():
            output = Path(kwargs.get("cwd", Path.cwd())) / output
        artifact = output.stat()
        if artifact.st_size == 0 or artifact.st_mtime_ns < started - 2_000_000_000:
            raise RuntimeError(f"Compiler produced a missing, empty or stale artifact: {output}")
        print(f"Verified fresh compile artifact: {output} ({artifact.st_mtime_ns})", flush=True)

def music_serialization_source(directory):
    source = (ROOT / "Core/Source/Lux/Scene/SceneSerializer.cpp").read_text()
    serialize = source.split('\t\t\tif (entity.HasComponent<MusicDirectorComponent>())', 1)[1].split('\t\t\tif (entity.HasComponent<AudioZoneComponent>())', 1)[0]
    deserialize = source.split('\t\t\t\tif (const auto node = entity["MusicDirectorComponent"])', 1)[1].split('\t\t\t\tif (const auto node = entity["AudioZoneComponent"])', 1)[0]
    output = directory / "music-serialization.cpp"
    output.write_text("""#include "lpch.h"
#include "Lux/Scene/Components.h"
#include "Lux/Audio/AudioEventRefSerializer.h"
#include "Lux/Serialization/Yaml.h"
#include <cassert>
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
namespace Lux
{
    std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("serialization");
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
}
using namespace Lux;
struct FakeEntity
{
    MusicDirectorComponent Music;
    template<typename T> T& GetComponent() { return Music; }
    template<typename T> T& AddComponent() { return Music; }
};
std::string SerializeMusic(FakeEntity entity)
{
    Yaml::Writer out;
    out << Yaml::BeginMap;
""" + serialize + """
    out << Yaml::EndMap;
    return out.c_str();
}
MusicDirectorComponent DeserializeMusic(const Yaml::Node& entity)
{
    FakeEntity deserializedEntity;
    if (auto node = entity["MusicDirectorComponent"])
""" + deserialize + """
    return deserializedEntity.Music;
}
""" + (TESTS / "MusicSerializationTests.cpp").read_text())
    return output

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Keep logs, objects and fixture in this directory")
    parser.add_argument("--config", choices=("debug", "debug-as"), default="debug", help="matching generated native configuration and dependencies")
    args = parser.parse_args()
    directory = args.output or Path(tempfile.mkdtemp(prefix="lux-audio-tests-"))
    directory.mkdir(parents=True, exist_ok=True)
    directory = directory.resolve()
    print("Test artifacts:", directory, flush=True)
    makefile = (ROOT / "Core/Makefile").read_text()
    # Each config block holds nested ifeq/endif (CC/CXX/AR defaults); end at the next config.
    marker = f"ifeq ($(config),{args.config})"
    if marker not in makefile:
        raise RuntimeError(f"Core/Makefile has no {args.config} configuration; regenerate gmake2 projects first")
    configuration = makefile.split(marker, 1)[1].split("else ifeq ($(config),", 1)[0]
    flags = shlex.split(re.search(r"^DEFINES \+= (.*)$", configuration, re.M)[1])
    flags = [flag for flag in flags if not flag.startswith(("-DTRACY", "-DLUX_TRACK_MEMORY"))]
    flags += shlex.split(re.search(r"^INCLUDES \+= (.*)$", configuration, re.M)[1])
    flags += ["-std=c++20", "-O0", "-g", "-ffunction-sections", "-fdata-sections"]
    if args.config == "debug-as":
        if not re.search(r"^ALL_CXXFLAGS[^\n]*-fsanitize=address", configuration, re.M):
            raise RuntimeError("Generated Core Debug-AS compilation is not instrumented with AddressSanitizer")
        flags += ["-fsanitize=address", "-fno-omit-frame-pointer"]
    native_configuration = "Debug-AS" if args.config == "debug-as" else "Debug"
    # Compile and link the same complete kernel set; adding a stage must not leave
    # an independent list of undefined symbols in the focused occlusion executable.
    kernel_sources = sorted((ROOT / "Core/vendor/soundwave/src").glob("*.cpp"))
    sources = [*("vendor/soundwave/src/" + path.stem for path in kernel_sources), "Audio/AudioGeometrySystem", "Audio/AudioAcoustics",
               "Core/JobSystem", "Audio/AudioOcclusion", "Audio/AudioOcclusionSettings", "Audio/AcousticMaterial", "Audio/AudioEventRefSerializer",
               "Core/UUID", "Core/Ref", "Physics/JoltPhysics/JoltContactListener", "Serialization/FileStream", "Serialization/AssetPackSerializer",
               "Serialization/StreamWriter", "Serialization/StreamReader", "Serialization/Yaml", "vendor/rapidyaml/ryml"]
    objects = {}
    for source in sources:
        obj = directory / (Path(source).name + ".o")
        with (directory / (obj.stem + ".log")).open("w") as log:
            run(["clang++", *flags, "-c", (source if source.startswith(("Platform/", "vendor/")) else "Source/Lux/" + source) + ".cpp", "-o", obj],
                cwd=ROOT / "Core", stdout=log, stderr=subprocess.STDOUT)
        objects[Path(source).name] = obj
    if args.config == "debug-as":
        symbols = subprocess.run(["nm", "--undefined-only", str(objects["AudioAcoustics"])],
                                 capture_output=True, text=True, check=True).stdout
        instrumented = sorted({line.split()[-1] for line in symbols.splitlines() if re.search(r"__asan_(report_|load|store)", line)})
        if not instrumented:
            raise RuntimeError("Production AudioAcoustics.o contains no AddressSanitizer memory-access instrumentation")
        (directory / "asan-audio-acoustics.log").write_text("\n".join(instrumented) + "\n", encoding="utf-8")
        print("Verified AddressSanitizer memory-access instrumentation: production AudioAcoustics.o", flush=True)
    run(["clang++", *flags, TESTS / "FileStreamTests.cpp", objects["FileStream"],
         "-pthread", "-Wl,--gc-sections", "-o", directory / "stream-test"], cwd=ROOT / "Core")
    run([directory / "stream-test", directory], timeout=30)
    run(["clang++", *flags, TESTS / "AssetPackFailureTests.cpp",
         *[objects[name] for name in ("AssetPackSerializer", "FileStream", "StreamWriter", "StreamReader", "UUID", "Ref")],
         "-pthread", "-Wl,--gc-sections", "-o", directory / "pack-test"], cwd=ROOT / "Core")
    run([directory / "pack-test", directory], timeout=30)
    jolt = ROOT / f"Core/vendor/JoltPhysics/bin/{native_configuration}-linux-x86_64/JoltPhysics/libJoltPhysics.a"
    run(["clang++", *flags, TESTS / "JoltContactTests.cpp", objects["JoltContactListener"], objects["UUID"],
         jolt, "-pthread", "-Wl,--gc-sections", "-o", directory / "jolt-test"], cwd=ROOT / "Core")
    run([directory / "jolt-test"], timeout=30)
    # Build the YAML implementation with the same flags as these tests, without stale engine objects.
    yaml = [objects[name] for name in ("Yaml", "ryml")]
    if args.config == "debug-as":
        # Verify the worker/snapshot ownership on its own first. The complete AS audio
        # suite still runs below, with leak detection enabled.
        lifetime_objects = [objects[path.stem] for path in kernel_sources] + [objects[name] for name in (
            "AudioAcoustics", "JobSystem", "AudioOcclusionSettings", "AcousticMaterial", "UUID", "Ref")]
        run(["clang++", *flags, TESTS / "AudioAcousticsTestMain.cpp", TESTS / "AudioAcousticsTests.cpp",
             *lifetime_objects, *yaml, "-Wl,--gc-sections", "-pthread", "-o", directory / "soundwave-lifetime-test"],
            cwd=ROOT / "Core")
        run([directory / "soundwave-lifetime-test"], timeout=30)
    # This test has a minimal logger host, matching the Windows occlusion check. Link its
    # production dependencies only; unrelated mixer globals require the full playback host.
    occlusion_objects = [objects[path.stem] for path in kernel_sources] + [objects[name] for name in (
        "AudioAcoustics", "JobSystem", "AudioOcclusion", "AudioOcclusionSettings",
        "AcousticMaterial", "AudioGeometrySystem", "UUID", "Ref",
        "FileStream", "StreamWriter", "StreamReader")]
    run(["clang++", *flags, TESTS / "OcclusionTests.cpp", TESTS / "AudioAcousticsTests.cpp", *occlusion_objects, *yaml,
         "-Wl,--gc-sections", "-pthread",
         "-o", directory / "occlusion-test"], cwd=ROOT / "Core")
    run([directory / "occlusion-test", directory], timeout=30)
    run(["clang++", *flags, music_serialization_source(directory), objects["Ref"], objects["UUID"], objects["AudioEventRefSerializer"], *yaml, "-Wl,--gc-sections",
         "-o", directory / "music-serialization-test"], cwd=ROOT / "Core")
    run([directory / "music-serialization-test"], timeout=30)


if __name__ == "__main__":
    main()
