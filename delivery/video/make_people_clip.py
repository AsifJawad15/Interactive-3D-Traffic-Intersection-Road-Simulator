"""Builds a short clip of people crossing on WALK, delivery/video/people_crossing.mp4,
to cut into the demo video in place of its people part (50-58 s).

The program's scout run finds the signalised crossing and the moment a WALK
light turns on with the most people waiting, and the most walking over the
zebra afterwards; the clip shows them waiting, the light, and the crossing
(10.5 s, 1920x1080, 30 FPS), with the same caption style as demo.mp4.

Run from anywhere:  python delivery/video/make_people_clip.py
"""

import subprocess
import sys

from make_video import BOLD, EXE, FFMPEG, HERE, PROJECT, REGULAR, drawtext

RAW = HERE / "people_raw.mp4"
FINAL = HERE / "people_crossing.mp4"


def main():
    if not EXE.exists():
        sys.exit(f"Build the Release program first: {EXE} is missing")
    subprocess.run([str(EXE), "--tour-people", str(RAW), "--ffmpeg", FFMPEG], cwd=PROJECT, check=True)

    filters = []
    captions = (RAW.parent / (RAW.name + ".captions.tsv")).read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(captions):
        start, end, title, subtitle = line.split("\t")
        start, end = float(start), float(end)
        filters.append(drawtext(f"p{index}t", title, BOLD, 46, 48, 44, start, end, color="0xFFD21A", border=16))
        filters.append(drawtext(f"p{index}s", subtitle, REGULAR, 30, 48, 128, start + 0.15, end, border=12))
    script = HERE / "people_captions.filter"
    script.write_text(",\n".join(filters), encoding="utf-8")
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", str(RAW), "-filter_script:v", str(script),
                    "-c:v", "libx264", "-preset", "slow", "-crf", "21", "-pix_fmt", "yuv420p",
                    "-movflags", "+faststart", str(FINAL)], check=True)
    print(f"Wrote {FINAL}")


if __name__ == "__main__":
    main()
