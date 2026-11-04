"""Regenerate the decoder fixtures. Developer-only: needs ffmpeg with flac, libvorbis and libmp3lame.

The tests read the committed files and never run this script. The WAV is written here so its
samples are known exactly; the other formats are encoded from it.
"""
import math
import pathlib
import struct
import subprocess
import wave

RATE = 48000
FRAMES = RATE // 2
LEFT_HZ, LEFT_AMPLITUDE = 440.0, 0.5
RIGHT_HZ, RIGHT_AMPLITUDE = 660.0, 0.25


def sample(hz, amplitude, frame):
    return round(amplitude * math.sin(2.0 * math.pi * hz * frame / RATE) * 32767)


def main():
    fixtures = pathlib.Path(__file__).resolve().parent / "fixtures"
    fixtures.mkdir(exist_ok=True)
    source = fixtures / "tone.wav"
    with wave.open(str(source), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(b"".join(
            struct.pack("<hh", sample(LEFT_HZ, LEFT_AMPLITUDE, i), sample(RIGHT_HZ, RIGHT_AMPLITUDE, i))
            for i in range(FRAMES)))
    common = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
              "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact"]
    subprocess.run(common + ["-c:a", "flac", str(fixtures / "tone.flac")], check=True)
    subprocess.run(common + ["-c:a", "libvorbis", "-q:a", "6", str(fixtures / "tone.ogg")], check=True)
    subprocess.run(common + ["-c:a", "libmp3lame", "-b:a", "192k", str(fixtures / "tone.mp3")], check=True)


if __name__ == "__main__":
    main()
