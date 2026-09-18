# Target platform: Windows 11+, portability-conscious stack

The legacy app targeted Windows 7 32-bit via Matrox MIL; the MIL license is gone and the camera hardware moved to Moticam, forcing a rebuild anyway. We're targeting Windows 11 and onward only — no Win7 support. Build stack is C++ with Qt (GUI) and OpenCV (capture/imaging) rather than Windows-only frameworks (WinForms/WPF/MFC), since a Linux build is plausible later but not committed now ("perhaps").

**Consequences:** Camera capture must sit behind an abstraction so the backend can swap (DirectShow → V4L2) without touching the rest of the app. Moticam's Linux driver support is unconfirmed at time of writing — this decision does not resolve that risk, it just contains it to one module instead of the whole codebase.
