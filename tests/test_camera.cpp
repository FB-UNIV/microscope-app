// User journey:
//   As an operator at a workstation with no camera attached (or before the
//   Motic SDK backend exists), I can still open the app on saved images;
//   and when the configured camera is missing I get a message that says
//   why, not a black preview.

#include "camera_factory.hpp"
#include "file_replay_camera.hpp"
#include "opencv_camera.hpp"

#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace msapp;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "  [PASS] " << description << "\n";
    } else {
        std::cout << "  [FAIL] " << description << "\n";
        ++g_failures;
    }
}

template <typename Exception, typename Fn>
std::string throwsWith(Fn fn) {
    try {
        fn();
    } catch (const Exception& e) {
        return e.what();
    }
    return {};
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_camera_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path frameA = dir / "a.png";
    const fs::path frameB = dir / "b.png";
    cv::imwrite(frameA.string(), cv::Mat(4, 6, CV_8UC3, cv::Scalar(10, 20, 30)));
    cv::imwrite(frameB.string(), cv::Mat(4, 6, CV_8UC3, cv::Scalar(200, 100, 50)));

    std::cout << "== FileReplayCamera: no-camera mode ==\n";
    {
        FileReplayCamera cam({frameA, frameB});
        check(cam.backend() == "file", "backend name is 'file'");
        check(!throwsWith<std::runtime_error>([&] { cam.grab(); }).empty(), "grab before open throws");
        cam.open();
        check(cam.isOpen(), "open succeeds when every frame exists");
        cv::Mat f1 = cam.grab();
        cv::Mat f2 = cam.grab();
        cv::Mat f3 = cam.grab();
        check(f1.at<cv::Vec3b>(0, 0) == cv::Vec3b(10, 20, 30), "first grab is the first frame (BGR)");
        check(f2.at<cv::Vec3b>(0, 0) == cv::Vec3b(200, 100, 50), "second grab is the second frame");
        check(f3.at<cv::Vec3b>(0, 0) == cv::Vec3b(10, 20, 30), "replay cycles back to the start");
        check(!cam.setExposureMs(10.0) && !cam.setWhiteBalance({}), "unsupported controls report false");
        cam.close();
        check(!cam.isOpen(), "close");
    }
    {
        FileReplayCamera missing({dir / "nope.png"});
        check(!throwsWith<std::runtime_error>([&] { missing.open(); }).empty(), "missing frame fails at open");
        FileReplayCamera empty({});
        check(!throwsWith<std::runtime_error>([&] { empty.open(); }).empty(), "no frames fails at open");
    }

    std::cout << "\n== OpenCvCamera: absent device explains itself ==\n";
    {
        OpenCvCamera cam(97);
        const std::string message = throwsWith<std::runtime_error>([&] { cam.open(); });
        check(message.find("ADR 0011") != std::string::npos,
              "open on a missing device throws, pointing at the Motic/WinUSB ADR");
        check(!cam.isOpen(), "camera stays closed");
    }

    std::cout << "\n== makeCamera: backend chosen by configuration ==\n";
    {
        CameraConfig fileConfig;
        fileConfig.backend = "file";
        fileConfig.frames = {frameA};
        check(makeCamera(fileConfig)->backend() == "file", "'file' -> FileReplayCamera");

        CameraConfig cvConfig;
        cvConfig.backend = "opencv";
        check(makeCamera(cvConfig)->backend() == "opencv", "'opencv' -> OpenCvCamera (not opened yet)");

        CameraConfig motic;
        motic.backend = "motic";
        check(throwsWith<std::runtime_error>([&] { makeCamera(motic); }).find("Motic SDK") != std::string::npos,
              "'motic' fails loudly until the SDK backend exists");

        CameraConfig bogus;
        bogus.backend = "matrox";
        check(!throwsWith<std::invalid_argument>([&] { makeCamera(bogus); }).empty(),
              "unknown backend throws invalid_argument");
    }

    fs::remove_all(dir);
    std::cout << "\n== " << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED")
              << " ==\n";
    return g_failures == 0 ? 0 : 1;
}
