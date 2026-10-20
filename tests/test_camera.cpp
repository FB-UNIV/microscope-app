// User journey:
//   As an operator at a workstation with no camera attached (or before the
//   Motic SDK backend exists), I can still open the app on saved images;
//   and when the configured camera is missing I get a message that says
//   why, not a black preview.

#include "camera_factory.hpp"
#include "file_replay_camera.hpp"
#include "jpeg_frame.hpp"
#include "opencv_camera.hpp"
#include "test_support.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

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
        // Each file is decoded once, not on every grab (a 4K PNG per frame
        // took a full core in --replay mode).
        const fs::path frameC = dir / "c.png";
        cv::imwrite(frameC.string(), cv::Mat(4, 6, CV_8UC3, cv::Scalar(1, 2, 3)));
        FileReplayCamera cam({frameC});
        cam.open();
        cv::Mat first = cam.grab();
        fs::remove(frameC);
        first.setTo(cv::Scalar(9, 9, 9));
        const cv::Mat again = cam.grab();
        check(again.at<cv::Vec3b>(0, 0) == cv::Vec3b(1, 2, 3),
              "a frame once decoded is replayed without reading its file again");
        check(first.at<cv::Vec3b>(0, 0) == cv::Vec3b(9, 9, 9) && again.data != first.data,
              "each grab is its own copy: changing one frame changes no other");
        check(!throwsWith<std::runtime_error>([&] { cam.open(); }).empty(),
              "open() looks at the files again, so reconnecting picks up a changed one");
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

        CameraConfig badFormat;
        badFormat.backend = "opencv";
        badFormat.pixelFormat = "MJ";
        check(!throwsWith<std::invalid_argument>([&] { makeCamera(badFormat); }).empty(),
              "a pixel format that isn't a 4-character FOURCC is rejected");
    }

    std::cout << "\n== Moticam A5 profile (measured with camera_probe) ==\n";
    {
        const CameraConfig a5 = moticamA5Profile();
        check(a5.backend == "opencv" && a5.captureApi == "dshow" && a5.deviceIndex == 0,
              "DirectShow at index 0 — the only API exposing white balance");
        check(a5.width == 3840 && a5.height == 2160 && a5.pixelFormat == "MJPG", "3840x2160 MJPG");
        check(a5.whiteBalanceKelvin && *a5.whiteBalanceKelvin == 5000.0, "colour temperature locked at 5000 K");
        check(a5.exposureMs && *a5.exposureMs == 7.8125,
              "exposure fixed at 7.8 ms, DirectShow's -7, as measured at x5 through the microscope");
        check(a5.whiteReference.isIdentity(), "White reference identity until calibrated");
        check(makeCamera(a5)->backend() == "opencv", "builds without a camera attached");
    }

    std::cout << "\n== CameraConfig JSON: the workstation config file ==\n";
    {
        CameraConfig c = moticamA5Profile();
        c.exposureMs = 12.5;
        c.whiteReference = WhiteBalance{0.6, 1.0, 1.7};
        const CameraConfig back = CameraConfig::fromJson(c.toJson());
        check(back.backend == c.backend && back.captureApi == c.captureApi && back.width == c.width &&
                  back.height == c.height && back.pixelFormat == c.pixelFormat && back.deviceIndex == c.deviceIndex,
              "round-trips backend, API, size, format, index");
        check(back.exposureMs && *back.exposureMs == 12.5 && back.whiteBalanceKelvin && *back.whiteBalanceKelvin == 5000.0,
              "round-trips exposure and colour temperature");
        check(back.whiteReference.red == 0.6 && back.whiteReference.blue == 1.7, "round-trips the White reference");
        check(c.toJson().at("exposure_ms").is_number() && CameraConfig{}.toJson().at("exposure_ms").is_null(),
              "unset exposure is written as null, not 0");

        const CameraConfig minimal = CameraConfig::fromJson(nlohmann::json{{"backend", "opencv"}});
        check(minimal.captureApi == "auto" && !minimal.exposureMs && minimal.whiteReference.isIdentity(),
              "missing keys take defaults, so older config files still load");

        CameraConfig files;
        files.frames = {dir / "a.png"};
        check(CameraConfig::fromJson(files.toJson()).frames == files.frames, "file-replay frame paths round-trip");
    }

    std::cout << "\n== JPEG frames read undecoded from an MJPG stream (#76) ==\n";
    {
        // Noise, with a restart marker after every MCU: the scan data holds
        // stuffed FF 00 bytes and FF D0-D7 markers, as a camera's may.
        cv::Mat noise(64, 64, CV_8UC3);
        cv::randu(noise, cv::Scalar::all(0), cv::Scalar::all(256));
        std::vector<unsigned char> jpeg;
        cv::imencode(".jpg", noise, jpeg, {cv::IMWRITE_JPEG_RST_INTERVAL, 1});
        const JpegExtent whole = findJpegExtent(jpeg.data(), jpeg.size());
        check(whole.complete() && whole.end == jpeg.size(), "an encoded JPEG runs from FF D8 to its last byte, FF D9");

        std::vector<unsigned char> padded = jpeg;
        padded.resize(jpeg.size() + 64, 0);
        padded.insert(padded.end(), jpeg.end() - 32, jpeg.end());
        check(findJpegExtent(padded.data(), padded.size()).end == jpeg.size(),
              "padding after the image is left out, even when it holds an older frame's FF D9");

        const JpegExtent cut = findJpegExtent(jpeg.data(), jpeg.size() - 1);
        check(cut.startsWithSoi && !cut.complete(), "a frame cut before its FF D9 is not complete");

        // An APP1 segment (where EXIF keeps its thumbnail) holding FF D9.
        std::vector<unsigned char> withApp1 = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x06, 0xFF, 0xD9, 0xFF, 0xD9};
        withApp1.insert(withApp1.end(), jpeg.begin() + 2, jpeg.end());
        check(findJpegExtent(withApp1.data(), withApp1.size()).end == withApp1.size(),
              "an FF D9 inside a segment does not end the image");

        const cv::Mat bgr(4, 6, CV_8UC3, cv::Scalar(10, 20, 30));
        check(!findJpegExtent(bgr.data, bgr.total() * bgr.elemSize()).startsWithSoi,
              "decoded BGR pixels are not JPEG");
        check(!findJpegExtent(nullptr, 0).startsWithSoi && !findJpegExtent(jpeg.data(), 1).startsWithSoi,
              "an empty or one-byte buffer is not JPEG");

        const cv::Mat half = cv::imdecode(jpeg, cv::IMREAD_REDUCED_COLOR_2);
        check(half.cols == 32 && half.rows == 32 && half.type() == CV_8UC3,
              "IMREAD_REDUCED_COLOR_2 decodes straight to half size, the live view's");
    }

    fs::remove_all(dir);
    return summary();
}
