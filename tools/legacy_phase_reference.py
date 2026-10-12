"""Independent numpy reference for the legacy phase-analysis algorithms.

Re-implements, from the reverse-engineering notes
(docs/legacy/reverse-engineering-report.md section 5), the legacy Metal app's auto-threshold + phase counting so the C++
port in src/phase_analysis.cpp can be cross-checked against a second,
independently written implementation. Prints the numbers that
tests/test_phase_analysis.cpp asserts on the legacy CTTM TIFFs.

This is NOT legacy output (the legacy exe needs Matrox MIL to run) — it is a
second implementation of the same recovered spec. Every float->int conversion
truncates, matching the legacy x87 control word (|0xC00).

Usage: python tools/legacy_phase_reference.py <image> [<image> ...]
"""
import sys

import cv2
import numpy as np

F32 = np.float32


def legacy_grey(bgr):
    s = bgr[..., 0].astype(np.uint32) + bgr[..., 1] + bgr[..., 2]
    return (s // 3).astype(np.uint8)


def auto_threshold(grey):
    g = grey.astype(np.int64)
    l, r = g[1:-1, :-2], g[1:-1, 2:]
    u, d = g[:-2, 1:-1], g[2:, 1:-1]
    grad = ((l - r) ** 2 + (u - d) ** 2) // 512
    mean4 = (l + r + u + d) // 4
    hg = np.bincount(grad.ravel(), minlength=256).astype(np.int64)
    h2 = np.bincount((grad * 256 + mean4).ravel(), minlength=65536).astype(np.int64).reshape(256, 256)

    # 0x4133f0: highest gradient bin above 0.5 % of the histogram peak
    floor_count = int(int(hg.max()) * 0.5 / 100.0)
    if floor_count == 0:
        floor_count = 2
    gmax = 255
    for v in range(255, 0, -1):
        if hg[v] > floor_count:
            gmax = v
            break

    # 0x4143c0: edge-weighted grey histogram
    gmin = min(gmax * 33 // 100, 254)
    gtop = 255
    for v in range(256):
        if hg[v] != 0:
            gtop = v
    h = np.zeros(256, dtype=np.int64)
    for gg in range(gmin, gtop):
        h += (gg - gmin) * h2[gg]
    h &= 0xFFFFFFFF

    # 0x4139d0: gaussian smoothing + band search
    weights = [F32(np.exp(np.float64(F32(-(k * k)) / F32(21.125)))) for k in range(-13, 14)]
    wsum = F32(0.0)
    for w in weights:
        wsum = F32(wsum + w)
    smooth = h.copy()
    for v in range(13, 242):
        acc = 0
        for i, k in enumerate(range(-13, 14)):
            total = F32(F32(float(acc)) + F32(F32(float(h[v + k])) * weights[i]))
            acc = int(np.trunc(np.float64(total))) & 0xFFFFFFFF
        smooth[v] = int(np.trunc(np.float64(F32(F32(float(acc)) / wsum)))) & 0xFFFFFFFF

    peak, threshold = 0, 127
    for v in range(255):
        if peak < smooth[v]:
            peak, threshold = int(smooth[v]), v
    level = int(peak * 70.72135785007073 / 100.0)
    for v in range(256):
        if level < smooth[v]:
            threshold = v
            break
    for v in range(255, -1, -1):
        if level < smooth[v]:
            threshold = (v + threshold) // 2
            break
    half, width = peak >> 1, 127
    for v in range(256):
        if half < smooth[v]:
            width = v
            break
    for v in range(255, -1, -1):
        if half < smooth[v]:
            width = int((v - width) / 2)
            break
    return threshold, int(width / 3)


def legacy_morph(img, op):
    k = np.ones((5, 5), bool)
    k[0, 0] = k[0, 4] = k[4, 0] = k[4, 4] = False
    win = np.lib.stride_tricks.sliding_window_view(img, (5, 5))
    win = win[..., k]
    inner = win.min(axis=-1) if op == "erode" else win.max(axis=-1)
    out = np.empty_like(img)
    out[2:-2, 2:-2] = inner
    out[:2, :] = out[2, :]
    out[-2:, :] = out[-3, :]
    out[:, :2] = out[:, 2:3]
    out[:, -2:] = out[:, -3:-2]
    return out


def count_phases(grey, threshold, tolerance):
    n = grey.size
    p = grey.astype(np.int32)
    high = int(np.count_nonzero(p > threshold + tolerance))
    band = int(np.count_nonzero((p >= threshold - tolerance) & (p <= threshold + tolerance)))
    dark = float(F32((float(F32(n - high - band // 2)) * 100.0) / float(F32(n))))
    light = float(F32((float(F32(band // 2 + high)) * 100.0) / float(F32(n))))
    error = float(F32((float(F32(band)) * 100.0) / float(F32(n * 2))))
    return dark, light, error


def analyze(path):
    img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if img.ndim == 2:
        grey = img
        threshold, tolerance = auto_threshold(grey)
        analysed = grey
    else:
        grey = legacy_grey(img[..., :3])
        threshold, tolerance = auto_threshold(grey)
        analysed = legacy_morph(grey, "erode")
        analysed = legacy_morph(analysed, "dilate")
        analysed = legacy_morph(analysed, "dilate")
        analysed = legacy_morph(analysed, "erode")
    dark, light, error = count_phases(analysed, threshold, tolerance)
    print(f"{path}: threshold={threshold} tolerance={tolerance} "
          f"dark={dark:.6f} light={light:.6f} error={error:.6f}")


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        analyze(arg)
