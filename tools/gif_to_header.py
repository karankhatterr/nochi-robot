#!/usr/bin/env python3
"""Turn any GIF into an emote header for the Nochi robot.

Usage:
    python3 gif_to_header.py input.gif 43 [--threshold 128] [--invert]

This scales every frame to fit the 128x64 OLED (centred on black),
converts it to pure black and white, writes the result as a new GIF
and stores those bytes in firmware/nochi_robot/src/emotes/<number>.h
as `const uint8_t _<number>[] PROGMEM`, the format the firmware plays
with the AnimatedGIF library.

Needs Pillow:  pip install pillow
"""
import argparse
import io
from pathlib import Path

from PIL import Image, ImageSequence

W, H = 128, 64
EMOTE_DIR = Path(__file__).resolve().parent.parent / "firmware" / "nochi_robot" / "src" / "emotes"


def convert(src, threshold, invert):
    frames, durations = [], []
    with Image.open(src) as im:
        for frame in ImageSequence.Iterator(im):
            g = frame.convert("L")
            g.thumbnail((W, H), Image.LANCZOS)
            canvas = Image.new("L", (W, H), 0)
            canvas.paste(g, ((W - g.width) // 2, (H - g.height) // 2))
            bw = canvas.point(lambda v: 255 if (v > threshold) != invert else 0)
            frames.append(bw.convert("P"))
            durations.append(max(frame.info.get("duration", 100), 20))
    out = io.BytesIO()
    frames[0].save(out, format="GIF", save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, disposal=1, optimize=False)
    return out.getvalue(), len(frames)


def write_header(data, number, n_frames, path):
    rows = [",".join("0x%02x" % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    text = (
        f"//\n// {number}\n// Data size = {len(data)} bytes\n//\n"
        f"// GIF, Size: {W} x {H}, {n_frames} frames (made with tools/gif_to_header.py)\n//\n"
        "#ifndef PROGMEM\n#define PROGMEM\n#endif\n"
        f"const uint8_t _{number}[] PROGMEM = {{\n\t" + ",\n\t".join(rows) + "\n};\n"
    )
    path.write_text(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gif")
    ap.add_argument("number", type=int, help="emote number; the array becomes _<number>")
    ap.add_argument("--threshold", type=int, default=128, help="0-255, brightness that counts as 'on'")
    ap.add_argument("--invert", action="store_true", help="swap black and white")
    args = ap.parse_args()

    data, n = convert(args.gif, args.threshold, args.invert)
    out = EMOTE_DIR / f"{args.number}.h"
    write_header(data, args.number, n, out)
    print(f"Wrote {out} ({n} frames, {len(data)} bytes)")
    print("Next steps:")
    print(f'  1. Add  #include "{args.number}.h"  to src/emotes/animation.h')
    print(f"  2. Add a case in loop() in nochi_robot.ino:")
    print(f"       case X: playWrapper((uint8_t*)_{args.number}, sizeof(_{args.number})); break;")
    print("     and raise NUMBEROFANIMATION if you added a new case number.")


if __name__ == "__main__":
    main()
