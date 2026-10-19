#pragma once

#include "camera.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace msapp {

// Replays a fixed list of image files, cycling forever. The legacy app had
// a "no camera" mode (1042x780 placeholder) for working on saved views; this
// is its replacement, and what tests and CI use instead of hardware.
//
// Each file is decoded on its first grab after open() and kept: decoding a
// 4K PNG on every grab took a full core in --replay mode. open() forgets
// them, so reconnecting picks up a file that changed. Every grab returns its
// own copy, as a camera would.
class FileReplayCamera : public ICamera {
public:
    explicit FileReplayCamera(std::vector<std::filesystem::path> frames);

    std::string backend() const override { return "file"; }
    void open() override;
    void close() override;
    bool isOpen() const override { return open_; }
    cv::Mat grab() override;

private:
    std::vector<std::filesystem::path> frames_;
    std::vector<cv::Mat> decoded_;  // frames_[i] once grabbed, empty until then
    std::size_t next_ = 0;
    bool open_ = false;
};

} // namespace msapp
