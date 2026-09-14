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
    # Separate 2D scores/stinger retain the same source sound, in a disposable project only.
    for name in ("MusicBed", "MusicOther", "MusicStinger"):
        text = ET.tostring(cloned, encoding="unicode")
        new_ids = {node.attrib["id"]: "{" + str(uuid.uuid4()) + "}" for node in cloned.findall("object")}
        for old, new in new_ids.items():
            text = text.replace(old, new)
        music = ET.fromstring(text)
        music_event = music.find("object[@class='Event']")
        music_event.find("property[@name='name']/value").text = name
        music.find("object[@class='EventAutomatableProperties']/property[@name='isPersistent']/value").text = str(name != "MusicStinger").lower()
        for spatial in list(music.findall("object[@class='SpatialiserEffect']")):
            for dest in music.findall(".//relationship[@name='effects']/destination"):
                if dest.text == spatial.attrib["id"]:
                    for rel in music.findall(".//relationship[@name='effects']"):
                        if dest in list(rel):
                            rel.remove(dest)
            music.remove(spatial)
        ET.ElementTree(music).write(target / "Metadata/Event" / (music_event.attrib["id"] + ".xml"), encoding="UTF-8", xml_declaration=True)
    alsa = directory / "asound.conf"
    alsa.write_text("pcm.!default { type null }\nctl.!default { type hw card 0 }\n")
    environment = dict(os.environ, QT_QPA_PLATFORM="minimal", ALSA_CONFIG_PATH=str(alsa))
    with (directory / "fixture-build.log").open("w") as log:
        run(["fmodstudiocl", "-build", "-ignore-warnings", "-log-dir", directory,
             "-script", TESTS / "AuthorFixture.js", target / "SampleProject.fspro"],
            env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    if "[script-error]" in (directory / "fixture-build.log").read_text():
        raise RuntimeError("FMOD fixture authoring failed; see fixture-build.log")
    return target / "Build/Desktop"

def music_serialization_source(directory):
    source = (ROOT / "Core/Source/Lux/Scene/SceneSerializer.cpp").read_text()
    serialize = source.split('\t\t\tif (entity.HasComponent<MusicDirectorComponent>())', 1)[1].split('\t\t\tif (entity.HasComponent<AudioZoneComponent>())', 1)[0]
    deserialize = source.split('\t\t\t\tif (auto node = entity["MusicDirectorComponent"])', 1)[1].split('\t\t\t\tif (auto node = entity["AudioZoneComponent"])', 1)[0]
    output = directory / "music-serialization.cpp"
    output.write_text("""#include "lpch.h"
#include "Lux/Scene/Components.h"
#include <yaml-cpp/yaml.h>
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
    YAML::Emitter out;
    out << YAML::BeginMap;
""" + serialize + """
    out << YAML::EndMap;
    return out.c_str();
}
MusicDirectorComponent DeserializeMusic(const YAML::Node& entity)
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
    sources = ["Audio/AudioEventInstance", "Audio/DialogueDirector", "Audio/DialogueTable", "Asset/DialogueTableSerializer", "Audio/MusicDirector", "Audio/PhysicsAudioSystem", "Audio/AudioSurfaceTable",
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
    run(["clang++", *flags, music_serialization_source(directory), objects["Ref"], *yaml, "-Wl,--gc-sections",
         "-o", directory / "music-serialization-test"], cwd=ROOT / "Core")
    run([directory / "music-serialization-test"], timeout=30)
    audio_objects = [obj for name, obj in objects.items() if name not in ("JoltContactListener", "AssetPackSerializer")]
    run(["clang++", *flags, TESTS / "PhysicsAudioTests.cpp", *audio_objects, *yaml, *libraries,
         *["-Wl,-rpath," + str(lib.parent) for lib in libraries], "-Wl,--gc-sections", "-pthread",
         "-o", directory / "audio-test"], cwd=ROOT / "Core")
    run([directory / "audio-test", banks, directory], timeout=30)
    run(["clang++", *flags, TESTS / "MusicTests.cpp", *audio_objects, *yaml, *libraries,
         *["-Wl,-rpath," + str(lib.parent) for lib in libraries], "-Wl,--gc-sections", "-pthread",
         "-o", directory / "music-test"], cwd=ROOT / "Core")
    run([directory / "music-test", banks], timeout=45)
    run(["clang++", *flags, TESTS / "DialogueTests.cpp", *audio_objects, *yaml, *libraries,
         *["-Wl,-rpath," + str(lib.parent) for lib in libraries], "-Wl,--gc-sections", "-pthread",
         "-o", directory / "dialogue-test"], cwd=ROOT / "Core")
    run([directory / "dialogue-test", banks, fmod / "studio/examples/media", directory], timeout=60)


if __name__ == "__main__":
    main()
