#include "measurement_marks.hpp"

namespace msapp {

std::size_t pointsNeeded(MeasureTool) { return 0; }

void MeasurementMarks::start(MeasureTool) {}
void MeasurementMarks::cancel() {}
void MeasurementMarks::clear() {}
bool MeasurementMarks::click(cv::Point2d) { return false; }
bool MeasurementMarks::moveHandle(int, cv::Point2d) { return false; }
void MeasurementMarks::removeHandle(int) {}
void MeasurementMarks::undo() {}
std::size_t MeasurementMarks::pendingPoints() const { return 0; }
std::vector<cv::Point2d> MeasurementMarks::handles() const { return {}; }
std::optional<std::pair<std::size_t, std::size_t>> MeasurementMarks::markPointAt(int) const { return std::nullopt; }

} // namespace msapp
