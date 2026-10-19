#pragma once

#include <opencv2/core/types.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace msapp {

// The tools that measure by clicking points on a Vue (#56).
enum class MeasureTool { Length, Vickers };

// Two points for a length, four for a Vickers indent (two diagonals).
std::size_t pointsNeeded(MeasureTool tool);

// One measurement clicked on the photo, in image pixels.
struct Mark {
    MeasureTool tool;
    std::vector<cv::Point2d> points;

    bool complete() const { return points.size() == pointsNeeded(tool); }
};

// The measurements clicked on a Vue in the viewer, and the tool clicking
// them (#56): kept so their points can be dragged and their values
// recomputed; gone when the window closes, like every Measurement. No Qt
// here, so it is tested with the core; the Vue window draws it.
//
// Finished Marks come first, then — while a tool is active — the pending
// one being clicked. A handle index is the N-th point across all Marks, in
// order: that is how the viewer numbers its draggable points.
class MeasurementMarks {
public:
    // Puts the active tool away first (dropping its half-done reading), then
    // starts an empty pending Mark for `tool`.
    void start(MeasureTool tool);
    // Échap: puts the tool away, dropping a half-done pending Mark.
    void cancel();
    // Effacer: no Mark, no reading shown, no tool.
    void clear();

    // Adds a point to the pending Mark. True when it finished the reading:
    // that Mark becomes current() and a new pending Mark of the same tool
    // starts. With no tool active, the click is ignored.
    bool click(cv::Point2d point);
    // A dragged point. True when its Mark is finished (its value changed):
    // that Mark becomes current().
    bool moveHandle(int index, cv::Point2d point);
    // Right-click on a point: a finished Mark goes whole; a point of the
    // pending one goes alone. current() follows the erasure, and is reset
    // when its Mark went.
    void removeHandle(int index);
    // Retour arrière: takes back the last point of the pending Mark — or, if
    // none was clicked yet, reopens the reading of the same tool just
    // finished (no longer current()) and takes back its last point.
    void undo();

    std::optional<MeasureTool> activeTool() const { return tool_; }
    const std::vector<Mark>& marks() const { return marks_; }
    // Points clicked on the pending Mark; 0 with no tool active.
    std::size_t pendingPoints() const;
    // The finished Mark whose value is shown.
    std::optional<std::size_t> current() const { return current_; }
    // Every Mark's points, in order: the viewer's handles.
    std::vector<cv::Point2d> handles() const;
    // The Mark and point that handle `index` stands for.
    std::optional<std::pair<std::size_t, std::size_t>> markPointAt(int index) const;

private:
    std::optional<MeasureTool> tool_;
    std::vector<Mark> marks_;
    std::optional<std::size_t> current_;
};

} // namespace msapp
