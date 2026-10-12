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
        check(message.find("index 97") != std::string::npos && message.find("MotiConnect") != std::string::npos,
              "open on a missing device names the index and the usual culprit (camera held by MotiConnect)");
        check(!cam.isOpen(), "camera stays closed");
        check(cam.frameSize().empty(), "frameSize() of a closed camera is empty, not a stale guess");
    }

    std::cout << "\n== captureApiFromName: Windows UVC needs a choosable capture API ==\n";
    {
        check(captureApiFromName("auto") == cv::CAP_ANY, "'auto' -> CAP_ANY");
        check(captureApiFromName("dshow") == cv::CAP_DSHOW, "'dshow' -> CAP_DSHOW (exposure/gain controls)");
        check(captureApiFromName("msmf") == cv::CAP_MSMF, "'msmf' -> CAP_MSMF");
        check(captureApiFromName("v4l2") == cv::CAP_V4L2, "'v4l2' -> CAP_V4L2 (Linux)");
        check(!throwsWith<std::invalid_argument>([&] { captureApiFromName("gstreamer2"); }).empty(),
              "unknown capture API throws invalid_argument");
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

        // The Moticam A5 is UVC (ADR 0013). The size is an example request,
        // not the A5's spec — confirm the real one with tools/camera_probe.
        CameraConfig uvc;
        uvc.backend = "opencv";
        uvc.captureApi = "dshow";
        uvc.width = 2592;
        uvc.height = 1944;
        check(makeCamera(uvc)->backend() == "opencv", "UVC config (DirectShow + requested size) builds");

        CameraConfig badApi;
        badApi.backend = "opencv";
        badApi.captureApi = "nope";
        check(!throwsWith<std::invalid_argument>([&] { makeCamera(badApi); }).empty(),
              "bad capture API is rejected when the camera is built, not at first grab");

        CameraConfig motic;
        motic.backend = "motic";
        const std::string moticMessage = throwsWith<std::runtime_error>([&] { makeCamera(motic); });
        check(moticMessage.find("\"opencv\"") != std::string::npos && moticMessage.find("ADR 0013") != std::string::npos,
              "'motic' explains the A5 needs backend \"opencv\" (ADR 0013)");

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
