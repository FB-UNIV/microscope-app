"""Synthetic image of the lab's 20 um mire, to try the calibration without a microscope.

Draws dark vertical lines every PITCH pixels on a bright, slightly noisy
background, at the A5's 3840x2160 by default, so a known px/um is expected
back: px/um = PITCH / 20. Feed it to the app as a camera, calibrate one
Grossissement in Poste > Calibrer un grossissement sur la mire, and check the
reading; then replay a lab frame to capture with that Calibration, or with
one derived from it (ADR 0020).

    python tools/make_mire.py --pitch 31 --out mire.png
    microscope_app.exe --replay mire.png

--pitch 31 gives 1.55 px/um, about what the A5 sees through a x5 objective
(probe round 2). Lines are blurred a little so they look like a photographed
reticle rather than single pixels.
"""
import argparse

import cv2
import numpy as np

MIRE_PITCH_MICRONS = 20.0


def make_mire(width, height, pitch, line_width, seed=0):
    rng = np.random.default_rng(seed)
    image = np.full((height, width), 215.0, dtype=np.float32)
    x = np.arange(width, dtype=np.float32)
    # Distance from each column to the nearest line centre.
    offset = (x - pitch / 2.0) % pitch
    distance = np.minimum(offset, pitch - offset)
    profile = np.clip(1.0 - (distance - line_width / 2.0), 0.0, 1.0)  # 1 on a line, anti-aliased edge
    image -= 170.0 * profile[np.newaxis, :]
    image = cv2.GaussianBlur(image, (0, 0), 1.2)
    image += rng.normal(0.0, 3.0, image.shape).astype(np.float32)
    grey = np.clip(image, 0, 255).astype(np.uint8)
    return cv2.cvtColor(grey, cv2.COLOR_GRAY2BGR)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pitch", type=float, default=31.0, help="pixels between two lines (default 31)")
    parser.add_argument("--size", default="3840x2160", help="WIDTHxHEIGHT (default the A5's 3840x2160)")
    parser.add_argument("--line-width", type=float, default=3.0, help="line width in pixels (default 3)")
    parser.add_argument("--out", default="mire.png", help="PNG to write (default mire.png)")
    args = parser.parse_args()

    width, height = (int(v) for v in args.size.lower().split("x"))
    if args.pitch <= args.line_width:
        parser.error("--pitch must be larger than --line-width")
    if not cv2.imwrite(args.out, make_mire(width, height, args.pitch, args.line_width)):
        raise SystemExit(f"cannot write {args.out}")
    print(f"{args.out}: {width}x{height}, one line every {args.pitch:g} px = "
          f"{args.pitch / MIRE_PITCH_MICRONS:.4f} px/um expected")


if __name__ == "__main__":
    main()
