#include "file_replay_camera.hpp"

#include <opencv2/imgcodecs.hpp>
#include <stdexcept>

namespace msapp {

FileReplayCamera::FileReplayCamera(std::vector<std::filesystem::path> frames) : frames_(std::move(frames)) {}

void FileReplayCamera::open() {
    if (frames_.empty()) {
        throw std::runtime_error("FileReplayCamera: no frames to replay");
    }
    for (const auto& frame : frames_) {
        if (!std::filesystem::exists(frame)) {
            throw std::runtime_error("FileReplayCamera: missing frame " + frame.string());
        }
    }
    decoded_.assign(frames_.size(), cv::Mat());
    next_ = 0;
    open_ = true;
}

void FileReplayCamera::close() {
    open_ = false;
}

cv::Mat FileReplayCamera::grab() {
    if (!open_) {
        throw std::runtime_error("FileReplayCamera::grab: camera is not open");
    }
    const std::size_t index = next_;
    next_ = (next_ + 1) % frames_.size();
    cv::Mat& image = decoded_[index];
    if (image.empty()) {
        image = cv::imread(frames_[index].string(), cv::IMREAD_COLOR);
        if (image.empty()) {
            throw std::runtime_error("FileReplayCamera::grab: could not decode " + frames_[index].string());
        }
    }
    return image.clone();
}

} // namespace msapp
