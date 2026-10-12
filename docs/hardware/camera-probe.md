# Running camera_probe on a lab PC

`camera_probe` checks, on the real lab PC, that the Moticam A5 is visible to the
app's capture backend (ADR 0013): which camera index it is, which Windows capture
API works, what resolution and pixel format it delivers, and whether a real frame
comes back. Nothing is installed; it runs from a folder and needs no admin rights.

## 1. Get the bundle

CI builds a ready-to-run Windows bundle on every push and pull request.

1. Open the repo on GitHub → **Actions** → latest green **CI** run on `master`.
2. Under **Artifacts**, download **`camera_probe-windows`** (a zip; you must be
   logged in to GitHub to download artifacts).
3. Copy the zip to the lab PC (USB stick is fine).

Or, from any machine with the GitHub CLI:

```
gh run download --repo FB-UNIV/microscope-app --name camera_probe-windows
```

The zip contains `camera_probe.exe`, the OpenCV DLL it needs, the Microsoft C++
runtime DLLs, and this README.

## 2. Prepare the lab PC

1. Right-click the zip → **Properties** → tick **Unblock** → OK (stops Windows
   SmartScreen from blocking an unsigned exe downloaded from the internet), then
   **Extract All** to e.g. `C:\camera_probe`.
2. Plug in the Moticam A5. In **Device Manager** it should be listed under
   **Cameras** (UVC). If it is under "Image" or "libusbK/WinUSB" instead, a Motic
   driver has taken it over — stop and report that.
3. **Close MotiConnect** (and any other camera app, Teams, etc.): a UVC camera can
   only be opened by one program at a time.

## 3. Run it

Open PowerShell in the folder (Shift + right-click in the folder → *Open PowerShell
window here*) and run these, in order. Each run saves one PNG per working camera in
its `--out-dir`, and `Tee-Object` keeps the console output in a text file.

```
.\camera_probe.exe --api dshow --out-dir probe-dshow | Tee-Object probe-dshow.txt
.\camera_probe.exe --api msmf  --out-dir probe-msmf  | Tee-Object probe-msmf.txt
```

Each camera index prints something like this (numbers illustrative, not the A5's):

```
index 0: opened via DSHOW
  default: 1024x768 format=MJPG reported_fps=30
  default controls: exposure=-7 auto_exposure=-1 gain=8 auto_wb=... wb_temperature=5000
  measured: 20/20 frames in 0.67 s = 29.9 fps
  frame 1024x768 x3 channels, mean BGR (...), clipped 0.2% -> probe-dshow\camera_probe_index0.png
```

Find the A5's index (the one whose PNG shows the microscope image, not a laptop
webcam). If its size is small, ask for a larger one with `--size`; `--mjpg` asks for
compressed frames, which USB cameras often need to reach full resolution at a usable
frame rate.

All options: `--api`, `--max-index N` (indexes to try, default 4), `--size WxH`,
`--mjpg`, `--frames N` (frames timed for the fps measurement, default 20),
`--auto-exposure V`, `--exposure V` (DirectShow: log2 seconds, -7 ≈ 7.8 ms, -8 ≈ 3.9 ms),
`--wb KELVIN`, `--out-dir DIR`. Values are read back after setting, so the output
shows whether the camera accepted them.

Exit code: 0 if at least one camera delivered a frame, 1 if none did, 2 for a bad
option.

## 4. Send back

The `*.txt` files and the PNGs of the A5. They decide the camera defaults
(capture API, resolution, pixel format, exposure) the Qt preview will use.

## Moticam A5: known so far

From the first lab run (probe v1, 2026-10-12, no specimen in focus):

| Item | Result |
|---|---|
| Index / APIs | index 0 is the A5; opens with both DirectShow and Media Foundation |
| Default mode | 1024×768 |
| Larger mode | **3840×2160 accepted and delivered** via DirectShow, with and without `--mjpg` |
| Controls | DirectShow: exposure -7 (≈ 7.8 ms), gain 8, white balance 5000 K readable; MSMF reports no white balance |
| Not yet known | pixel format (v1 could not decode it), real frame rate at 4K, whether manual exposure / white balance stick |
| Image check | frames were a featureless pink field; at 4K 15–40 % of pixels clipped (overexposed) |

### Round 2 (probe v2) — with a real sample

Put a polished sample under the microscope, focus it (MotiConnect live view is
fine for focusing), **then close MotiConnect** and run:

```
.\camera_probe.exe --api dshow --size 3840x2160 --out-dir r2-4k | Tee-Object r2-4k.txt
.\camera_probe.exe --api dshow --size 3840x2160 --mjpg --out-dir r2-4k-mjpg | Tee-Object r2-4k-mjpg.txt
.\camera_probe.exe --api dshow --size 1920x1080 --mjpg --out-dir r2-1080 | Tee-Object r2-1080.txt
.\camera_probe.exe --api dshow --size 3840x2160 --mjpg --exposure -9 --out-dir r2-exp9 | Tee-Object r2-exp9.txt
.\camera_probe.exe --api dshow --size 3840x2160 --mjpg --wb 4500 --out-dir r2-wb4500 | Tee-Object r2-wb4500.txt
```

If the `--exposure -9` run's "after request" line still shows the old exposure, the
camera stayed in auto exposure: retry adding `--auto-exposure 0`, then
`--auto-exposure 0.25` (backends disagree on the value meaning "manual").
Send back the five `r2-*.txt` files and the `r2-4k-mjpg` and `r2-exp9` PNGs.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| "Windows protected your PC" | Zip not unblocked — *More info → Run anyway*, or unblock and re-extract (step 2.1). |
| Missing `opencv_world*.dll` / `VCRUNTIME140.dll` / `MSVCP140.dll` | Run the exe from inside the extracted folder; don't copy the exe alone. |
| `index N: nothing` for every index | Camera not plugged in, held by MotiConnect, or not UVC (step 2.2). Try `--api msmf`. |
| Opens but `no frame delivered` | Another program grabbed the camera, or the requested `--size` is unsupported — retry without `--size`. |
| Size stays small after `--size` | Mode not offered over that API/format — retry with `--mjpg`, or with `--api msmf`. |
| Blocked by IT policy (AppLocker) | Ask IT to allow `camera_probe.exe` from that folder. |
