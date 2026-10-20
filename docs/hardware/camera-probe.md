# Running camera_probe on a lab PC

`camera_probe` checks, on the real lab PC, that the Moticam A5 is visible to the
app's capture backend (ADR 0013): which camera index it is, which Windows capture
API works, what resolution and pixel format it delivers, and whether a real frame
comes back. Nothing is installed; it runs from a folder and needs no admin rights.

## 1. Get the bundle

Every green build of `master` publishes a ready-to-run Windows bundle on the
repo's Releases page.

1. Open [Releases › latest](https://github.com/FB-UNIV/microscope-app/releases/tag/latest)
   (the newest green build of `master`; a `vX.Y.Z` release is a build that stays put).
2. Under **Assets**, download **`camera_probe-windows-<build>.zip`**.
3. Copy the zip to the lab PC (USB stick is fine), or download it there directly.

Or, from any machine with the GitHub CLI:

```
gh release download latest --repo FB-UNIV/microscope-app --pattern 'camera_probe-windows-*.zip'
```

The zip contains `camera_probe.exe`, the OpenCV DLL it needs, the Microsoft C++
runtime DLLs, and this README. The first line of every report names the build,
as the zip does (`camera_probe v0.1.0: OpenCV 4.13.0 ...`); `camera_probe --version`
prints it alone.

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
`--wb KELVIN`, `--out-dir DIR`, `--version` (prints the build and exits). Values are
read back after setting, so the output shows whether the camera accepted them.

Exit code: 0 if at least one camera delivered a frame, 1 if none did, 2 for a bad
option.

## 4. Send back

The `*.txt` files and the PNGs of the A5. They decide the camera defaults
(capture API, resolution, pixel format, exposure) the Qt preview will use.

## Moticam A5: known so far

From three lab runs: probe v1 on 2026-10-12 and probe v2 on 2026-10-15 with the camera
pointed at a plain wall, then round 2 on 2026-10-16 through the microscope, on a
polished ferrite–pearlite steel at x5 plus one run at x10:

| Item | Result |
|---|---|
| Index / APIs | index 0 is the A5; opens with both DirectShow and Media Foundation. Use **DirectShow**: only it exposes white balance. |
| Default mode | 1024×768, format `0xE436EB7D` (believed to be DirectShow's RGB24 subtype; first 32 bits of its GUID) |
| HD modes | **3840×2160 and 1920×1080 come as MJPG** whether or not `--mjpg` is passed; frames show JPEG 8×8 block edges (lossy) |
| Frame rate | through the microscope: **4K MJPG 13–15 fps** at exposure -7 to -9, **1080p MJPG 30.6 fps** at -7. The ~10 fps of the wall runs was auto exposure. DirectShow reports fps as 0 |
| Exposure | manual `--exposure` works with no `--auto-exposure` flag (`0` and `0.25` change nothing); `auto_exposure` is not readable through DirectShow (always -1). At x5, **-7 (7.8 ms) fills the histogram with no clipping** (mean BGR 108 / 137 / 180, 0.0004 % clipped); -9 is darker (82 / 104 / 137). At x10 with the lamp turned up, -8 clipped 82.6 %: brightness follows the lamp knob. The profile fixes -7 (`moticamA5Profile()`). |
| White balance | manual `--wb` works (auto_wb → 0, temperature read back, 4500 K accepted); **auto WB is unstable** — the same wall came out pink in one run and lavender in the next, always green-deficient. Under the microscope lamp, 5000 K plus the White reference gave colours the teacher found right. |
| Persistence | **settings survive between programs**: a run started at exposure -9 left by the previous run. The app must apply its full camera profile on every open. Unplug/replug to reset. |
| Resolution | at 1:1 the 4K frame is soft, with only a little more fine texture than 1080p enlarged ×2 (some of it JPEG noise): probably upscaled or limited by the optics at x5. 4K is kept: Calibrations are tied to it (ADR 0015) and 15 fps is enough. |
| Dust | the x5 frame shows blurred dark blobs, out of focus while the sample is sharp: dust on the optics. Phase analysis counts them as dark phase (#55). |

A `workstation.json` saved before exposure was fixed holds `"exposure_ms": null` (camera
auto) and keeps it: set the exposure to 7.8 ms in *Poste › Configurer la caméra* (or
*Rétablir le profil Moticam A5* there) to take the measured value.

### Round 2 (probe v2) — with a real sample

Ran on 2026-10-16; its results are in the table above. To repeat it on another
PC or camera, put a polished sample under the microscope, focus it (MotiConnect
live view is fine for focusing), **then close MotiConnect** and run:

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
