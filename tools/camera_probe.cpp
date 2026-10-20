// First-contact check for a lab PC: which camera indexes open, through
// which capture API, at what frame size, pixel format and frame rate, how
// exposure / white balance respond, and whether a real frame comes back.
// Saves one PNG per working camera. Run it with the Moticam A5 plugged in
// (and MotiConnect closed) before wiring the UI. See
// docs/hardware/camera-probe.md.
//
//   camera_probe [--api auto|dshow|msmf|v4l2] [--max-index N]
//                [--size WIDTHxHEIGHT] [--mjpg] [--raw-mjpg] [--frames N]
//                [--auto-exposure V] [--exposure V] [--wb KELVIN]
//                [--out-dir DIR]
//   camera_probe --version
//
// Exposure values are passed to OpenCV as-is: with DirectShow they are log2
// seconds (-7 = 1/128 s ~ 7.8 ms); auto-exposure semantics vary by backend.
//
// --raw-mjpg (issue #76) asks for MJPG with CAP_PROP_CONVERT_RGB off, then
// reports whether read() hands over the camera's JPEG undecoded, what a read
// costs, and how long decoding that JPEG takes at full and at half size. It
// saves the half-size frame instead of the full one.
//
// Exit code 0 when at least one camera delivered a frame, 1 otherwise.

#include "build_version.hpp"
#include "camera_factory.hpp"
#include "image_io.hpp"
#include "jpeg_frame.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string api = "auto";
    int maxIndex = 4;
    cv::Size size;
    bool mjpg = false;
    bool rawMjpg = false;
    int frames = 20;
    std::optional<double> autoExposure;
    std::optional<double> exposure;
    std::optional<double> wbKelvin;
    fs::path outDir = ".";
    bool version = false;
};

// Printable four-character code when it is one (MJPG, YUY2, NV12), hex
// otherwise — Media Foundation and some drivers return non-ASCII codes.
std::string fourccToString(double value) {
    const auto code = static_cast<unsigned int>(static_cast<long long>(value));
    if (code == 0) return "n/a";
    std::string s;
    bool printable = true;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((code >> (8 * i)) & 0xFF);
        printable &= c >= 0x20 && c < 0x7F;
        s += c;
    }
    if (printable) return s;
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%08X", code);
    return hex;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--api") {
            o.api = value();
        } else if (arg == "--max-index") {
            o.maxIndex = std::stoi(value());
        } else if (arg == "--size") {
            int w = 0, h = 0;
            if (std::sscanf(value().c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
                throw std::invalid_argument("--size expects WIDTHxHEIGHT, e.g. 3840x2160");
            }
            o.size = {w, h};
        } else if (arg == "--mjpg") {
            o.mjpg = true;
        } else if (arg == "--raw-mjpg") {
            o.rawMjpg = true;
        } else if (arg == "--frames") {
            o.frames = std::stoi(value());
            if (o.frames < 1) throw std::invalid_argument("--frames must be >= 1");
        } else if (arg == "--auto-exposure") {
            o.autoExposure = std::stod(value());
        } else if (arg == "--exposure") {
            o.exposure = std::stod(value());
        } else if (arg == "--wb") {
            o.wbKelvin = std::stod(value());
        } else if (arg == "--out-dir") {
            o.outDir = value();
        } else if (arg == "--version") {
            o.version = true;
        } else {
            throw std::invalid_argument("unknown option " + arg);
        }
    }
    return o;
}

void printMode(const char* label, cv::VideoCapture& cap) {
    std::cout << "  " << label << ": " << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
              << cap.get(cv::CAP_PROP_FRAME_HEIGHT) << " format=" << fourccToString(cap.get(cv::CAP_PROP_FOURCC))
              << " reported_fps=" << cap.get(cv::CAP_PROP_FPS) << "\n";
}

void printControls(const char* label, cv::VideoCapture& cap) {
    std::cout << "  " << label << ": exposure=" << cap.get(cv::CAP_PROP_EXPOSURE)
              << " auto_exposure=" << cap.get(cv::CAP_PROP_AUTO_EXPOSURE)
              << " gain=" << cap.get(cv::CAP_PROP_GAIN)
              << " auto_wb=" << cap.get(cv::CAP_PROP_AUTO_WB)
              << " wb_temperature=" << cap.get(cv::CAP_PROP_WB_TEMPERATURE) << "\n";
}

// Share of pixels with any channel >= 250: high values mean overexposure,
// which ruins phase analysis (clipped phases merge).
double clippedPercent(const cv::Mat& frame) {
    cv::Mat maxChannel;
    if (frame.channels() == 1) {
        maxChannel = frame;
    } else {
        std::vector<cv::Mat> channels;
        cv::split(frame, channels);
        maxChannel = channels[0].clone();
        for (std::size_t i = 1; i < channels.size(); ++i) cv::max(maxChannel, channels[i], maxChannel);
    }
    return 100.0 * cv::countNonZero(maxChannel >= 250) / static_cast<double>(frame.total());
}

// CPU time the whole process has used so far, every thread included: a
// capture backend decodes on threads of its own. std::clock() counts that
// on Linux, but wall time with MSVC.
double processCpuSeconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    const auto seconds = [](const FILETIME& t) {  // FILETIME counts 100 ns
        return static_cast<double>((static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-7;
    };
    return seconds(kernel) + seconds(user);
#else
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::string milliseconds(double seconds) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f ms", 1000.0 * seconds);
    return text;
}

std::size_t byteCount(const cv::Mat& m) {
    return m.total() * m.elemSize();
}

// A frame's bytes read as a JPEG, whatever shape the backend gave them
// (Media Foundation hands a raw buffer over as one row of CV_8UC1).
msapp::JpegExtent jpegExtentOf(const cv::Mat& frame) {
    if (frame.empty() || !frame.isContinuous()) return {};
    return msapp::findJpegExtent(frame.data, byteCount(frame));
}

// --raw-mjpg, after the first frame came: what read() returns with
// CONVERT_RGB off, what a read costs, and what decoding the JPEG costs.
void reportRawMjpg(cv::VideoCapture& cap, const cv::Mat& first, const Options& opt, int index) {
    std::cout << "  read() returns " << first.rows << "x" << first.cols << " " << cv::typeToString(first.type())
              << ", " << byteCount(first) << " bytes\n";
    const msapp::JpegExtent extent = jpegExtentOf(first);
    if (!extent.startsWithSoi) {
        std::cout << "  not JPEG: no FF D8 at byte 0, so " << cap.getBackendName()
                  << " decoded the stream itself and CONVERT_RGB off changed nothing\n";
    } else if (!extent.complete()) {
        std::cout << "  JPEG: FF D8 at byte 0, but no FF D9: the frame is truncated\n";
    } else {
        std::cout << "  JPEG: FF D8 at byte 0, FF D9 ends it at byte " << extent.end << " of " << byteCount(first)
                  << "\n";
    }

    // All the reads first, the decodes after: decoding between reads would
    // eat into the frame interval that read() waits out.
    std::vector<cv::Mat> jpegs;
    int delivered = 0;
    const double cpuStart = processCpuSeconds();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < opt.frames; ++i) {
        cv::Mat frame;  // a new buffer each time, so it is kept without a copy
        if (!cap.read(frame) || frame.empty()) continue;
        ++delivered;
        // Only the first two bytes here: walking the whole buffer would
        // count towards the read's CPU.
        if (frame.isContinuous() && byteCount(frame) >= 2 && frame.data[0] == 0xFF && frame.data[1] == 0xD8) {
            jpegs.push_back(frame);
        }
    }
    const double seconds = secondsSince(start);
    const double cpuSeconds = processCpuSeconds() - cpuStart;
    std::cout << "  measured: " << delivered << "/" << opt.frames << " frames in " << seconds << " s = "
              << (seconds > 0 ? delivered / seconds : 0.0) << " fps";
    if (delivered > 0) {
        std::cout << "; per frame, read() " << milliseconds(seconds / delivered) << ", CPU "
                  << milliseconds(cpuSeconds / delivered) << " (whole process)";
    }
    std::cout << "\n";
    if (jpegs.empty()) return;

    int complete = 0;
    int decoded = 0;
    double fullSeconds = 0.0;
    double halfSeconds = 0.0;
    cv::Size fullSize;
    cv::Mat half;
    for (const cv::Mat& frame : jpegs) {
        const msapp::JpegExtent e = jpegExtentOf(frame);
        if (!e.complete()) continue;
        ++complete;
        const cv::Mat jpeg(1, static_cast<int>(e.end), CV_8UC1, frame.data);  // without what follows FF D9
        auto t = std::chrono::steady_clock::now();
        const cv::Mat f = cv::imdecode(jpeg, cv::IMREAD_COLOR);
        fullSeconds += secondsSince(t);
        t = std::chrono::steady_clock::now();
        const cv::Mat h = cv::imdecode(jpeg, cv::IMREAD_REDUCED_COLOR_2);
        halfSeconds += secondsSince(t);
        if (f.empty() || h.empty()) continue;
        ++decoded;
        fullSize = f.size();
        half = h;
    }
    std::cout << "  JPEG frames: " << jpegs.size() << "/" << delivered << " start with FF D8, " << complete
              << " end with FF D9, " << decoded << " decoded\n";
    if (decoded == 0) return;
    std::cout << "  decode per frame: full size " << milliseconds(fullSeconds / complete) << " -> " << fullSize.width
              << "x" << fullSize.height << ", half size " << milliseconds(halfSeconds / complete) << " -> "
              << half.cols << "x" << half.rows << "\n";

    const fs::path out = opt.outDir / ("camera_probe_index" + std::to_string(index) + "_half.png");
    std::string saved = out.u8string();
    try {
        msapp::writeImage(out, half, {}, "camera_probe");
    } catch (const std::exception& e) {
        saved = std::string("not saved: ") + e.what();
    }
    std::cout << "  half-size frame -> " << saved << "\n";
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    int apiId = cv::CAP_ANY;
    try {
        opt = parse(argc, argv);
        apiId = msapp::captureApiFromName(opt.api);
    } catch (const std::exception& e) {
        std::cerr << "camera_probe: " << e.what() << "\n"
                  << "usage: camera_probe [--api auto|dshow|msmf|v4l2] [--max-index N] [--size WIDTHxHEIGHT] "
                     "[--mjpg] [--raw-mjpg] [--frames N] [--auto-exposure V] [--exposure V] [--wb KELVIN] "
                     "[--out-dir DIR]\n"
                     "       camera_probe --version\n";
        return 2;
    }
    if (opt.version) {
        std::cout << "camera_probe " << msapp::kBuildVersion << "\n";
        return 0;
    }
    fs::create_directories(opt.outDir);
    // First line of every report: which build this is, and the OpenCV it was
    // built with and runs on.
    std::cout << "camera_probe " << msapp::kBuildVersion << ": OpenCV " << CV_VERSION << " (runtime "
              << cv::getVersionString() << ")\n";

    int working = 0;
    for (int index = 0; index < opt.maxIndex; ++index) {
        cv::VideoCapture cap;
        if (!cap.open(index, apiId)) {
            std::cout << "index " << index << ": nothing\n";
            continue;
        }
        std::cout << "index " << index << ": opened via " << cap.getBackendName() << "\n";
        printMode("default", cap);
        printControls("default controls", cap);

        if (opt.mjpg || opt.rawMjpg) cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        if (!opt.size.empty()) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, opt.size.width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, opt.size.height);
        }
        if (opt.rawMjpg) {
            const bool accepted = cap.set(cv::CAP_PROP_CONVERT_RGB, 0);
            std::cout << "  convert_rgb: set to 0 " << (accepted ? "accepted" : "refused") << ", reads back "
                      << cap.get(cv::CAP_PROP_CONVERT_RGB) << "\n";
        }
        if (opt.mjpg || opt.rawMjpg || !opt.size.empty()) printMode("after request", cap);

        // Order matters on most UVC drivers: leave auto mode before setting a manual value.
        if (opt.autoExposure) cap.set(cv::CAP_PROP_AUTO_EXPOSURE, *opt.autoExposure);
        if (opt.exposure) cap.set(cv::CAP_PROP_EXPOSURE, *opt.exposure);
        if (opt.wbKelvin) {
            cap.set(cv::CAP_PROP_AUTO_WB, 0);
            cap.set(cv::CAP_PROP_WB_TEMPERATURE, *opt.wbKelvin);
        }
        if (opt.autoExposure || opt.exposure || opt.wbKelvin) printControls("after request", cap);

        // Some UVC cameras return empty frames while the stream starts up.
        cv::Mat frame;
        for (int attempt = 0; attempt < 10 && frame.empty(); ++attempt) cap.read(frame);
        if (frame.empty()) {
            std::cout << "  no frame delivered\n";
            continue;
        }
        if (opt.rawMjpg) {
            reportRawMjpg(cap, frame, opt, index);
            ++working;
            continue;
        }

        // Measured, not reported: what a live preview would actually get.
        int delivered = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < opt.frames; ++i) {
            if (cap.read(frame) && !frame.empty()) ++delivered;
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "  measured: " << delivered << "/" << opt.frames << " frames in " << seconds << " s = "
                  << (seconds > 0 ? delivered / seconds : 0.0) << " fps\n";

        const cv::Scalar mean = cv::mean(frame);
        const fs::path out = opt.outDir / ("camera_probe_index" + std::to_string(index) + ".png");
        std::string saved = out.u8string();
        try {
            msapp::writeImage(out, frame, {}, "camera_probe");
        } catch (const std::exception& e) {
            saved = std::string("not saved: ") + e.what();
        }
        std::cout << "  frame " << frame.cols << "x" << frame.rows << " x" << frame.channels()
                  << " channels, mean BGR (" << mean[0] << ", " << mean[1] << ", " << mean[2]
                  << "), clipped " << clippedPercent(frame) << "% -> " << saved << "\n";
        ++working;
    }

    std::cout << (working > 0 ? std::to_string(working) + " camera(s) delivered frames\n"
                              : std::string("no camera delivered a frame\n"));
    return working > 0 ? 0 : 1;
}
