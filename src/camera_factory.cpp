#include "camera_factory.hpp"
#include "file_replay_camera.hpp"
#include "opencv_camera.hpp"

#include <stdexcept>

namespace msapp {

std::unique_ptr<ICamera> makeCamera(const CameraConfig& config) {
    if (config.backend == "file") {
        return std::make_unique<FileReplayCamera>(config.frames);
    }
    if (config.backend == "opencv") {
        return std::make_unique<OpenCvCamera>(config.deviceIndex);
    }
    if (config.backend == "motic") {
        throw std::runtime_error(
            "makeCamera: the Motic SDK backend is not built yet — waiting on the Motic developer SDK "
            "(ADR 0011). Use backend \"file\" for no-camera work meanwhile.");
    }
    throw std::invalid_argument("makeCamera: unknown camera backend '" + config.backend + "'");
}

} // namespace msapp
