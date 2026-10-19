// User journey:
//   As a student in the Vue viewer, I pick the Longueur or Dureté Vickers
//   tool and click points on the photo: two for a length, four for an
//   indent, then the next reading starts at once. I drag a point to correct
//   it, right-click a point to delete its measurement, press Retour arrière
//   to take back the last point (even reopening the reading I just
//   finished), and Échap puts the tool away without leaving a half-done
//   reading behind (#56).

#include "measurement_marks.hpp"
#include "test_support.hpp"

#include <iostream>

using namespace msapp;
using namespace msapp::test;

namespace {

bool samePoint(cv::Point2d a, cv::Point2d b) {
    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y);
}

// A finished length from (0,0) to (10,0), the Length tool still out.
MeasurementMarks oneLength() {
    MeasurementMarks marks;
    marks.start(MeasureTool::Length);
    marks.click({0, 0});
    marks.click({10, 0});
    return marks;
}

} // namespace

int main() {
    std::cout << "== Points needed per tool ==\n";
    {
        check(pointsNeeded(MeasureTool::Length) == 2, "a length takes 2 points");
        check(pointsNeeded(MeasureTool::Vickers) == 4, "a Vickers indent takes 4 points (two diagonals)");
    }

    std::cout << "== Clicking a length, then the next one starts ==\n";
    {
        MeasurementMarks marks;
        check(!marks.activeTool() && marks.marks().empty(), "no tool, no Mark at first");

        marks.start(MeasureTool::Length);
        check(marks.activeTool() == MeasureTool::Length, "the Length tool is active");
        check(marks.marks().size() == 1 && marks.pendingPoints() == 0, "an empty pending Mark waits for its first point");

        check(!marks.click({1, 2}), "the first point does not finish the reading");
        check(marks.pendingPoints() == 1 && !marks.current(), "one point clicked, no reading shown yet");

        check(marks.click({11, 2}), "the second point finishes the reading");
        check(marks.marks().size() == 2, "a new pending Mark started after the finished one");
        check(marks.marks()[0].complete() && samePoint(marks.marks()[0].points[1], {11, 2}),
              "the finished Mark holds both points");
        check(marks.marks()[1].tool == MeasureTool::Length && marks.pendingPoints() == 0,
              "the new pending Mark uses the same tool");
        check(marks.current() == std::size_t{0}, "the reading just finished is the one shown");
    }

    std::cout << "== A Vickers indent takes four points ==\n";
    {
        MeasurementMarks marks;
        marks.start(MeasureTool::Vickers);
        check(!marks.click({0, 0}) && !marks.click({10, 0}) && !marks.click({5, -5}), "three points: not done yet");
        check(marks.click({5, 5}), "the fourth point finishes the indent");
        check(marks.marks()[0].tool == MeasureTool::Vickers && marks.marks()[0].points.size() == 4,
              "the indent holds four points");
        check(!marks.click({20, 20}) && marks.pendingPoints() == 1, "the next indent is being clicked");
    }

    std::cout << "== Clicks with no tool are ignored ==\n";
    {
        MeasurementMarks marks;
        check(!marks.click({3, 3}) && marks.marks().empty(), "no tool: a click adds nothing");
    }

    std::cout << "== Handles: every Mark's points, in order ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.click({50, 50});
        const std::vector<cv::Point2d> handles = marks.handles();
        check(handles.size() == 3 && samePoint(handles[0], {0, 0}) && samePoint(handles[1], {10, 0}) &&
                  samePoint(handles[2], {50, 50}),
              "three handles: the finished length's two points, then the pending point");
        check(marks.markPointAt(0) == std::make_pair(std::size_t{0}, std::size_t{0}), "handle 0 = Mark 0, point 0");
        check(marks.markPointAt(1) == std::make_pair(std::size_t{0}, std::size_t{1}), "handle 1 = Mark 0, point 1");
        check(marks.markPointAt(2) == std::make_pair(std::size_t{1}, std::size_t{0}), "handle 2 = Mark 1, point 0");
        check(!marks.markPointAt(3) && !marks.markPointAt(-1), "no Mark behind an index out of range");
    }

    std::cout << "== Dragging a handle corrects that point ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.cancel();
        check(marks.moveHandle(1, {20, 0}), "moving a point of a finished reading changes its value");
        check(samePoint(marks.marks()[0].points[1], {20, 0}), "the point is where it was dropped");
        check(marks.current() == std::size_t{0}, "the corrected reading is the one shown");

        MeasurementMarks pending;
        pending.start(MeasureTool::Length);
        pending.click({1, 1});
        check(!pending.moveHandle(0, {2, 2}), "moving a point of a half-done reading gives no value yet");
        check(samePoint(pending.marks()[0].points[0], {2, 2}), "but the point still moves");
        check(!pending.moveHandle(5, {0, 0}), "an unknown handle moves nothing");
    }

    std::cout << "== Right-click: a finished reading goes whole, a pending point goes alone ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.click({30, 0});
        marks.click({40, 0});  // second length finished; the pending Mark is index 2
        marks.click({60, 0});  // one point on the pending Mark
        check(marks.current() == std::size_t{1}, "the second length is the one shown");

        marks.removeHandle(4);  // the pending point
        check(marks.marks().size() == 3 && marks.pendingPoints() == 0, "only that point of the pending Mark went");

        marks.removeHandle(1);  // a point of the first length
        check(marks.marks().size() == 2 && samePoint(marks.marks()[0].points[0], {30, 0}),
              "the first length went whole");
        check(marks.current() == std::size_t{0}, "the shown reading's index follows the erasure");

        marks.removeHandle(0);
        check(marks.marks().size() == 1 && !marks.current(), "deleting the shown reading leaves none shown");
        check(marks.activeTool() == MeasureTool::Length && marks.pendingPoints() == 0,
              "the tool stays out, its pending Mark untouched");

        marks.removeHandle(7);
        check(marks.marks().size() == 1, "an unknown handle removes nothing");
    }

    std::cout << "== Right-click with no tool active ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.cancel();
        marks.removeHandle(1);
        check(marks.marks().empty() && !marks.current(), "the last finished reading goes whole, not one point");
    }

    std::cout << "== Retour arrière takes back the last point ==\n";
    {
        MeasurementMarks marks;
        marks.start(MeasureTool::Vickers);
        marks.click({0, 0});
        marks.click({10, 0});
        marks.undo();
        check(marks.pendingPoints() == 1 && samePoint(marks.marks()[0].points[0], {0, 0}),
              "the last point of the pending Mark goes");
        marks.undo();
        marks.undo();
        check(marks.marks().size() == 1 && marks.pendingPoints() == 0, "nothing left to take back: no change");
    }

    std::cout << "== Retour arrière reopens the reading just finished ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.undo();
        check(marks.marks().size() == 1 && marks.pendingPoints() == 1, "the finished length is pending again");
        check(samePoint(marks.marks()[0].points[0], {0, 0}), "with its first point kept");
        check(!marks.current(), "its value is no longer shown");
        check(marks.click({12, 0}) && samePoint(marks.marks()[0].points[1], {12, 0}),
              "clicking again finishes it with the new point");
    }

    std::cout << "== Retour arrière keeps a reading of the other tool ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.start(MeasureTool::Vickers);  // drops the empty pending Length
        check(marks.marks().size() == 2 && marks.marks()[1].tool == MeasureTool::Vickers,
              "the finished length stays, an empty Vickers Mark waits");
        marks.undo();
        check(marks.marks().size() == 2 && marks.marks()[0].complete(), "the length is not reopened by the Vickers tool");
    }

    std::cout << "== Retour arrière with no tool does nothing ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.cancel();
        marks.undo();
        check(marks.marks().size() == 1 && marks.marks()[0].complete(), "the finished length stays");
    }

    std::cout << "== Échap drops a half-done reading only ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.click({50, 50});
        marks.cancel();
        check(!marks.activeTool(), "the tool is put away");
        check(marks.marks().size() == 1 && marks.marks()[0].complete(), "the finished length stays, the half-done one goes");
        check(marks.current() == std::size_t{0}, "the finished length is still the one shown");

        marks.cancel();
        check(marks.marks().size() == 1, "cancelling twice changes nothing");
    }

    std::cout << "== Starting a tool drops the previous tool's half-done reading ==\n";
    {
        MeasurementMarks marks;
        marks.start(MeasureTool::Length);
        marks.click({1, 1});
        marks.start(MeasureTool::Vickers);
        check(marks.marks().size() == 1 && marks.marks()[0].tool == MeasureTool::Vickers && marks.pendingPoints() == 0,
              "only an empty Vickers Mark is left");
    }

    std::cout << "== Effacer clears everything ==\n";
    {
        MeasurementMarks marks = oneLength();
        marks.clear();
        check(marks.marks().empty() && !marks.current() && !marks.activeTool(), "no Mark, no reading, no tool");
    }

    return summary();
}
