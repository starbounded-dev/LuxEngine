#!/usr/bin/env python3
"""Linux headless audio regression checks using the installed FMOD and Jolt SDKs."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import uuid
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent

def run(command, **kwargs):
    subprocess.run(list(map(str, command)), check=True, **kwargs)

def fixture(directory):
    target = directory / "fixture"
    source = ROOT / "Editor/LuxSampleProject/Assets/Audio/SampleProject"
    shutil.copytree(source, target, ignore=shutil.ignore_patterns(".cache", ".user", "Build"))
    event_file = next((target / "Metadata/Event").glob("*.xml"))
    tree = ET.parse(event_file)
    objects = tree.getroot()
    event = objects.find("object[@class='Event']")
    event.find("property[@name='name']/value").text = "SurfaceMotion"
    properties = objects.find("object[@class='EventAutomatableProperties']")
    persistent = properties.find("property[@name='isPersistent']")
    if persistent is None:
        persistent = ET.SubElement(properties, "property", name="isPersistent")
        ET.SubElement(persistent, "value")
    persistent.find("value").text = "true"
    tree.write(event_file, encoding="UTF-8", xml_declaration=True)
    text = ET.tostring(objects, encoding="unicode")
    ids = {node.attrib["id"]: "{" + str(uuid.uuid4()) + "}" for node in objects.findall("object")}
    for old, new in ids.items():
        text = text.replace(old, new)
    cloned = ET.fromstring(text)
    cloned.find("object[@class='Event']/property[@name='name']/value").text = "SurfaceHit"
    cloned.find("object[@class='EventAutomatableProperties']/property[@name='isPersistent']/value").text = "false"
    ET.ElementTree(cloned).write(target / "Metadata/Event" / (ids[event.attrib["id"]] + ".xml"), encoding="UTF-8", xml_declaration=True)
    alsa = directory / "asound.conf"
    alsa.write_text("pcm.!default { type null }\nctl.!default { type hw card 0 }\n")
    environment = dict(os.environ, QT_QPA_PLATFORM="minimal", ALSA_CONFIG_PATH=str(alsa))
    with (directory / "fixture-build.log").open("w") as log:
        run(["fmodstudiocl", "-build", "-ignore-warnings", "-log-dir", directory,
             "-script", TESTS / "AuthorFixture.js", target / "SampleProject.fspro"],
            env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    return target / "Build/Desktop"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--banks", type=Path, help="Existing fixture banks; otherwise build a disposable FMOD project")
    parser.add_argument("--output", type=Path, help="Keep logs, objects and fixture in this directory")
    args = parser.parse_args()
    directory = args.output or Path(tempfile.mkdtemp(prefix="lux-audio-tests-"))
    directory.mkdir(parents=True, exist_ok=True)
    directory = directory.resolve()
    print("Test artifacts:", directory, flush=True)
    makefile = (ROOT / "Core/Makefile").read_text()
    debug = makefile.split("ifeq ($(config),debug)", 1)[1].split("endif", 1)[0]
    flags = shlex.split(re.search(r"^DEFINES \+= (.*)$", debug, re.M)[1])
    flags = [flag for flag in flags if not flag.startswith(("-DTRACY", "-DLUX_TRACK_MEMORY"))]
    flags += shlex.split(re.search(r"^INCLUDES \+= (.*)$", debug, re.M)[1])
    flags += ["-std=c++20", "-O0", "-g", "-ffunction-sections", "-fdata-sections"]
    sources = ["Audio/AudioEventInstance", "Audio/PhysicsAudioSystem", "Audio/AudioSurfaceTable",
               "Audio/AcousticMaterial", "Asset/AudioSurfaceTableSerializer", "Utilities/StringUtils", "Core/UUID", "Core/Ref",
               "Physics/JoltPhysics/JoltContactListener", "Serialization/FileStream", "Serialization/AssetPackSerializer", "Serialization/StreamWriter", "Serialization/StreamReader"]
    objects = {}
    for source in sources:
        obj = directory / (Path(source).name + ".o")
        with (directory / (obj.stem + ".log")).open("w") as log:
            run(["clang++", *flags, "-c", "Source/Lux/" + source + ".cpp", "-o", obj],
                cwd=ROOT / "Core", stdout=log, stderr=subprocess.STDOUT)
        objects[Path(source).name] = obj
    run(["clang++", *flags, TESTS / "FileStreamTests.cpp", objects["FileStream"],
         "-pthread", "-Wl,--gc-sections", "-o", directory / "stream-test"], cwd=ROOT / "Core")
    run([directory / "stream-test", directory], timeout=30)
    run(["clang++", *flags, TESTS / "AssetPackFailureTests.cpp",
         *[objects[name] for name in ("AssetPackSerializer", "FileStream", "StreamWriter", "StreamReader", "UUID", "Ref")],
         "-pthread", "-Wl,--gc-sections", "-o", directory / "pack-test"], cwd=ROOT / "Core")
    run([directory / "pack-test", directory], timeout=30)
    jolt = ROOT / "Core/vendor/JoltPhysics/bin/Debug-linux-x86_64/JoltPhysics/libJoltPhysics.a"
    run(["clang++", *flags, TESTS / "JoltContactTests.cpp", objects["JoltContactListener"], objects["UUID"],
         jolt, "-pthread", "-Wl,--gc-sections", "-o", directory / "jolt-test"], cwd=ROOT / "Core")
    run([directory / "jolt-test"], timeout=30)
    banks = args.banks.resolve() if args.banks else fixture(directory)
    fmod = ROOT / "Core/vendor/FMOD/fmodstudioapi20314linux/api"
    libraries = [fmod / "core/lib/x86_64/libfmod.so", fmod / "studio/lib/x86_64/libfmodstudio.so"]
    yaml = [ROOT / "bin-int/Release-linux-x86_64/Core" / (p.stem + ".o") for p in (ROOT / "Core/vendor/yaml-cpp/src").glob("*.cpp")]
    audio_objects = [obj for name, obj in objects.items() if name not in ("JoltContactListener", "AssetPackSerializer")]
    run(["clang++", *flags, TESTS / "PhysicsAudioTests.cpp", *audio_objects, *yaml, *libraries,
         *["-Wl,-rpath," + str(lib.parent) for lib in libraries], "-Wl,--gc-sections", "-pthread",
         "-o", directory / "audio-test"], cwd=ROOT / "Core")
    run([directory / "audio-test", banks, directory], timeout=30)

if __name__ == "__main__":
    main()
