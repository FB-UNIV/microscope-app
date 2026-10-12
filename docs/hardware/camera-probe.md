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

Each camera index prints something like:

```
index 0: opened via DSHOW, default 640x480 YUY2
  exposure=-6 auto_exposure=... gain=... wb_temperature=...
  frame 640x480 x3 channels, mean BGR (...) -> probe-dshow\camera_probe_index0.png
```

Find the A5's index (the one whose PNG shows the microscope image, not a laptop
webcam). If its size is small (e.g. 640x480), ask for the full resolution — take
the value from the A5 spec sheet; `2592x1944` below is only an example:

```
.\camera_probe.exe --api dshow --size 2592x1944 --out-dir probe-full | Tee-Object probe-full.txt
.\camera_probe.exe --api dshow --size 2592x1944 --mjpg --out-dir probe-mjpg | Tee-Object probe-mjpg.txt
```

`--mjpg` asks for compressed frames, which USB 2 cameras often need to reach full
resolution. Other options: `--max-index N` (indexes to try, default 4).

Exit code: 0 if at least one camera delivered a frame, 1 if none did, 2 for a bad
option.

## 4. Send back

The `probe-*.txt` files and the PNGs of the A5. They decide the camera defaults
(capture API, resolution, pixel format) the Qt preview will use.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| "Windows protected your PC" | Zip not unblocked — *More info → Run anyway*, or unblock and re-extract (step 2.1). |
| Missing `opencv_world*.dll` / `VCRUNTIME140.dll` / `MSVCP140.dll` | Run the exe from inside the extracted folder; don't copy the exe alone. |
| `index N: nothing` for every index | Camera not plugged in, held by MotiConnect, or not UVC (step 2.2). Try `--api msmf`. |
| Opens but `no frame delivered` | Another program grabbed the camera, or the requested `--size` is unsupported — retry without `--size`. |
| Size stays small after `--size` | Mode not offered over that API/format — retry with `--mjpg`, or with `--api msmf`. |
| Blocked by IT policy (AppLocker) | Ask IT to allow `camera_probe.exe` from that folder. |
