#include "measurement_marks.hpp"

#include <cstddef>

namespace msapp {

std::size_t pointsNeeded(MeasureTool tool) { return tool == MeasureTool::Vickers ? 4 : 2; }

void MeasurementMarks::start(MeasureTool tool) {
    cancel();  // drops what the previous tool left half-done
    tool_ = tool;
    marks_.push_back(Mark{tool, {}});
}

void MeasurementMarks::cancel() {
    if (!marks_.empty() && !marks_.back().complete()) marks_.pop_back();
    tool_.reset();
}

void MeasurementMarks::clear() {
    tool_.reset();
    marks_.clear();
    current_.reset();
}

bool MeasurementMarks::click(cv::Point2d point) {
    if (!tool_) return false;
    Mark& mark = marks_.back();
    mark.points.push_back(point);
    if (!mark.complete()) return false;
    current_ = marks_.size() - 1;
    marks_.push_back(Mark{*tool_, {}});  // the tool stays out for the next one
    return true;
}

bool MeasurementMarks::moveHandle(int index, cv::Point2d point) {
    const auto at = markPointAt(index);
    if (!at) return false;
    Mark& mark = marks_[at->first];
    mark.points[at->second] = point;
    if (!mark.complete()) return false;
    current_ = at->first;
    return true;
}

void MeasurementMarks::removeHandle(int index) {
    const auto at = markPointAt(index);
    if (!at) return;
    const std::size_t i = at->first;
    if (tool_ && i + 1 == marks_.size()) {  // the pending Mark: that point alone
        marks_[i].points.erase(marks_[i].points.begin() + static_cast<std::ptrdiff_t>(at->second));
        return;
    }
    marks_.erase(marks_.begin() + static_cast<std::ptrdiff_t>(i));
    if (current_ == i) {
        current_.reset();
    } else if (current_ && *current_ > i) {
        --*current_;
    }
}

void MeasurementMarks::undo() {
    if (!tool_) return;
    // Nothing clicked yet on the new reading: take back the one just finished.
    if (marks_.back().points.empty() && marks_.size() >= 2 && marks_[marks_.size() - 2].tool == *tool_) {
        marks_.pop_back();
        if (current_ == marks_.size() - 1) current_.reset();
    }
    if (!marks_.back().points.empty()) marks_.back().points.pop_back();
}

std::size_t MeasurementMarks::pendingPoints() const { return tool_ ? marks_.back().points.size() : 0; }

std::vector<cv::Point2d> MeasurementMarks::handles() const {
    std::vector<cv::Point2d> handles;
    for (const Mark& mark : marks_) handles.insert(handles.end(), mark.points.begin(), mark.points.end());
    return handles;
}

std::optional<std::pair<std::size_t, std::size_t>> MeasurementMarks::markPointAt(int index) const {
    if (index < 0) return std::nullopt;
    std::size_t remaining = static_cast<std::size_t>(index);
    for (std::size_t i = 0; i < marks_.size(); ++i) {
        if (remaining < marks_[i].points.size()) return std::make_pair(i, remaining);
        remaining -= marks_[i].points.size();
    }
    return std::nullopt;
}

} // namespace msapp
