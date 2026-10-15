// First-contact check for a lab PC: which camera indexes open, through
// which capture API, at what frame size, pixel format and frame rate, how
// exposure / white balance respond, and whether a real frame comes back.
// Saves one PNG per working camera. Run it with the Moticam A5 plugged in
// (and MotiConnect closed) before wiring the UI. See
// docs/hardware/camera-probe.md.
//
//   camera_probe [--api auto|dshow|msmf|v4l2] [--max-index N]
//                [--size WIDTHxHEIGHT] [--mjpg] [--frames N]
//                [--auto-exposure V] [--exposure V] [--wb KELVIN]
//                [--out-dir DIR]
//
// Exposure values are passed to OpenCV as-is: with DirectShow they are log2
// seconds (-7 = 1/128 s ~ 7.8 ms); auto-exposure semantics vary by backend.
//
// Exit code 0 when at least one camera delivered a frame, 1 otherwise.

#include "camera_factory.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string api = "auto";
    int maxIndex = 4;
    cv::Size size;
    bool mjpg = false;
    int frames = 20;
    std::optional<double> autoExposure;
    std::optional<double> exposure;
    std::optional<double> wbKelvin;
    fs::path outDir = ".";
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
                     "[--mjpg] [--frames N] [--auto-exposure V] [--exposure V] [--wb KELVIN] [--out-dir DIR]\n";
        return 2;
    }
    fs::create_directories(opt.outDir);
    // First line of every report: the OpenCV this bundle was built with and runs on.
    std::cout << "camera_probe: OpenCV " << CV_VERSION << " (runtime " << cv::getVersionString() << ")\n";

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

        if (opt.mjpg) cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        if (!opt.size.empty()) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, opt.size.width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, opt.size.height);
        }
        if (opt.mjpg || !opt.size.empty()) printMode("after request", cap);

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
        cv::imwrite(out.string(), frame);
        std::cout << "  frame " << frame.cols << "x" << frame.rows << " x" << frame.channels()
                  << " channels, mean BGR (" << mean[0] << ", " << mean[1] << ", " << mean[2]
                  << "), clipped " << clippedPercent(frame) << "% -> " << out.string() << "\n";
        ++working;
    }

    std::cout << (working > 0 ? std::to_string(working) + " camera(s) delivered frames\n"
                              : std::string("no camera delivered a frame\n"));
    return working > 0 ? 0 : 1;
}
