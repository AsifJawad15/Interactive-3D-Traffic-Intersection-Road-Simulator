"""Builds the two-minute demo video, delivery/video/demo.mp4.

1. The program records its scripted tour (--tour): 1920x1080, 30 FPS, every
   frame exactly 1/30 s of city time, so the video is smooth on any PC.
2. ffmpeg adds the title card and the captions (Segoe UI) with fades.

Run from anywhere:  python delivery/video/make_video.py
Needs:              pip install imageio-ffmpeg   (a bundled ffmpeg)
                    the Release build: x64/Release/OpenGLMiniProject.exe
"""

import subprocess
import sys
from pathlib import Path

import imageio_ffmpeg

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
EXE = PROJECT / "x64" / "Release" / "OpenGLMiniProject.exe"
RAW = HERE / "tour_raw.mp4"
FINAL = HERE / "demo.mp4"
TEXT = HERE / "text"
FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()

COURSE = "CSE 4102: Graphics Lab"
TITLE = "3D Smart City with Digital Traffic Signals"
BOLD = "C\\:/Windows/Fonts/segoeuib.ttf"
REGULAR = "C\\:/Windows/Fonts/segoeui.ttf"
FADE = 0.4


def fade(start, end):
    """Alpha rising over FADE seconds from start and falling to 0 at end."""
    return (f"if(lt(t,{start:.2f}),0,if(lt(t,{start + FADE:.2f}),(t-{start:.2f})/{FADE},"
            f"if(lt(t,{end - FADE:.2f}),1,if(lt(t,{end:.2f}),({end:.2f}-t)/{FADE},0))))")


def text_file(name, text):
    TEXT.mkdir(exist_ok=True)
    path = TEXT / f"{name}.txt"
    path.write_text(text, encoding="utf-8")
    return str(path).replace("\\", "/").replace(":", "\\:")


def drawtext(name, text, font, size, x, y, start, end, color="white", box="black@0.55", border=14):
    return (f"drawtext=fontfile='{font}':textfile='{text_file(name, text)}':fontsize={size}:fontcolor={color}"
            f":x={x}:y={y}:box=1:boxcolor={box}:boxborderw={border}:alpha='{fade(start, end)}'"
            f":enable='between(t,{start:.2f},{end:.2f})'")


def main():
    if not EXE.exists():
        sys.exit(f"Build the Release program first: {EXE} is missing")

    # 1. Record the tour (the program must run from the project folder for
    #    its shaders and textures).
    subprocess.run([str(EXE), "--tour", str(RAW), "--ffmpeg", FFMPEG], cwd=PROJECT, check=True)

    # 2. Title card and captions.
    filters = [
        drawtext("course", COURSE, BOLD, 40, "(w-text_w)/2", "h*0.36", 0.5, 5.4, color="0xFFD21A", border=18),
        drawtext("title", TITLE, BOLD, 64, "(w-text_w)/2", "h*0.36+80", 0.7, 5.4, border=22),
    ]
    captions = (RAW.parent / (RAW.name + ".captions.tsv")).read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(captions):
        start, end, title, subtitle = line.split("\t")
        start, end = float(start), float(end)
        filters.append(drawtext(f"c{index}t", title, BOLD, 46, 48, 44, start, end, color="0xFFD21A", border=16))
        filters.append(drawtext(f"c{index}s", subtitle, REGULAR, 30, 48, 128, start + 0.15, end, border=12))
    script = HERE / "captions.filter"
    script.write_text(",\n".join(filters), encoding="utf-8")

    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", str(RAW), "-filter_script:v", str(script),
                    "-c:v", "libx264", "-preset", "slow", "-crf", "21", "-pix_fmt", "yuv420p",
                    "-movflags", "+faststart", str(FINAL)], check=True)
    print(f"Wrote {FINAL}")


if __name__ == "__main__":
    main()
