// First-contact check for a lab PC: which camera indexes open, through
// which capture API, at what frame size and pixel format, and whether a
// real frame comes back. Saves one PNG per working camera. Run it with the
// Moticam A5 plugged in (and MotiConnect closed) before wiring the UI.
//
//   camera_probe [--api auto|dshow|msmf|v4l2] [--max-index N]
//                [--size WIDTHxHEIGHT] [--mjpg] [--out-dir DIR]
//
// Exit code 0 when at least one camera delivered a frame, 1 otherwise.

#include "camera_factory.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string api = "auto";
    int maxIndex = 4;
    cv::Size size;
    bool mjpg = false;
    fs::path outDir = ".";
};

std::string fourccToString(double value) {
    const int code = static_cast<int>(value);
    if (code <= 0) return "?";
    std::string s;
    for (int i = 0; i < 4; ++i) s += static_cast<char>((code >> (8 * i)) & 0xFF);
    return s;
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
                throw std::invalid_argument("--size expects WIDTHxHEIGHT, e.g. 2592x1944");
            }
            o.size = {w, h};
        } else if (arg == "--mjpg") {
            o.mjpg = true;
        } else if (arg == "--out-dir") {
            o.outDir = value();
        } else {
            throw std::invalid_argument("unknown option " + arg);
        }
    }
    return o;
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
                  << "usage: camera_probe [--api auto|dshow|msmf|v4l2] [--max-index N] "
                     "[--size WIDTHxHEIGHT] [--mjpg] [--out-dir DIR]\n";
        return 2;
    }
    fs::create_directories(opt.outDir);

    int working = 0;
    for (int index = 0; index < opt.maxIndex; ++index) {
        cv::VideoCapture cap;
        if (!cap.open(index, apiId)) {
            std::cout << "index " << index << ": nothing\n";
            continue;
        }
        std::cout << "index " << index << ": opened via " << cap.getBackendName() << ", default "
                  << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x" << cap.get(cv::CAP_PROP_FRAME_HEIGHT)
                  << " " << fourccToString(cap.get(cv::CAP_PROP_FOURCC)) << "\n";

        if (opt.mjpg) cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        if (!opt.size.empty()) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, opt.size.width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, opt.size.height);
        }
        if (opt.mjpg || !opt.size.empty()) {
            std::cout << "  after request: " << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
                      << cap.get(cv::CAP_PROP_FRAME_HEIGHT) << " "
                      << fourccToString(cap.get(cv::CAP_PROP_FOURCC)) << "\n";
        }
        std::cout << "  exposure=" << cap.get(cv::CAP_PROP_EXPOSURE)
                  << " auto_exposure=" << cap.get(cv::CAP_PROP_AUTO_EXPOSURE)
                  << " gain=" << cap.get(cv::CAP_PROP_GAIN)
                  << " wb_temperature=" << cap.get(cv::CAP_PROP_WB_TEMPERATURE) << "\n";

        // Some UVC cameras return empty frames while the stream starts up.
        cv::Mat frame;
        for (int attempt = 0; attempt < 10 && frame.empty(); ++attempt) cap.read(frame);
        if (frame.empty()) {
            std::cout << "  no frame delivered\n";
            continue;
        }
        const cv::Scalar mean = cv::mean(frame);
        const fs::path out = opt.outDir / ("camera_probe_index" + std::to_string(index) + ".png");
        cv::imwrite(out.string(), frame);
        std::cout << "  frame " << frame.cols << "x" << frame.rows << " x" << frame.channels()
                  << " channels, mean BGR (" << mean[0] << ", " << mean[1] << ", " << mean[2]
                  << ") -> " << out.string() << "\n";
        ++working;
    }

    std::cout << (working > 0 ? std::to_string(working) + " camera(s) delivered frames\n"
                              : std::string("no camera delivered a frame\n"));
    return working > 0 ? 0 : 1;
}
