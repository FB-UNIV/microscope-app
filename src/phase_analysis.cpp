#include "phase_analysis.hpp"

#include <opencv2/imgproc.hpp>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace msapp {

namespace {

// Legacy conversions ran through x87 FISTP with the rounding-control bits
// forced to "chop" (control word | 0xC00): truncate toward zero, keep the
// low 32 bits. Every float->int step below goes through this on purpose.
std::uint32_t truncToU32(double value) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
}

// legacySmooth's disk is 5x5: a smaller image can't be analysed.
constexpr int kMinPhaseSide = 5;

// The factor that brings a frame of `size` to the legacy width; 1 for a
// frame no wider (ADR 0021).
double legacyScale(const cv::Size& size) {
    return size.width > kLegacyFrameWidth ? static_cast<double>(kLegacyFrameWidth) / size.width : 1.0;
}

void requireGrey8(const cv::Mat& image, int minSide, const char* who) {
    if (image.type() != CV_8UC1 || image.rows < minSide || image.cols < minSide) {
        throw std::invalid_argument(std::string(who) + ": expected CV_8UC1 of at least " +
                                    std::to_string(minSide) + "x" + std::to_string(minSide));
    }
}

// module_histo_grad_x_y_niv (0x414100): squared central-difference gradient
// scaled by 1/512, plus a 2-D histogram of (gradient, mean of the 4 neighbours).
void gradientHistograms(const cv::Mat& grey, std::array<std::uint32_t, 256>& gradientHisto,
                        std::vector<std::uint32_t>& gradientByLevel) {
    for (int r = 1; r < grey.rows - 1; ++r) {
        const uchar* up = grey.ptr<uchar>(r - 1);
        const uchar* row = grey.ptr<uchar>(r);
        const uchar* down = grey.ptr<uchar>(r + 1);
        for (int c = 1; c < grey.cols - 1; ++c) {
            const int left = row[c - 1];
            const int right = row[c + 1];
            const int above = up[c];
            const int below = down[c];
            const int gx = left - right;
            const int gy = above - below;
            const int gradient = (gx * gx + gy * gy) / 512;
            ++gradientHisto[gradient];
            ++gradientByLevel[gradient * 256 + (left + right + above + below) / 4];
        }
    }
}

// 0x4133f0: highest gradient bin whose count exceeds 0.5 % of the peak bin.
int strongGradientBin(const std::array<std::uint32_t, 256>& gradientHisto) {
    std::uint32_t peak = gradientHisto[0];
    for (int v = 1; v < 256; ++v) {
        if (peak < gradientHisto[v]) peak = gradientHisto[v];
    }
    std::uint32_t floorCount = truncToU32(static_cast<double>(peak) * 0.5 / 100.0);
    if (floorCount == 0) floorCount = 2;
    for (int v = 255; v >= 1; --v) {
        if (floorCount < gradientHisto[v]) return v;
    }
    return 255;
}

// 0x4143c0: grey-level histogram of edge pixels, each weighted by how far its
// gradient sits above 33 % of the strong-gradient bin. The upper bound is the
// last non-empty gradient bin (exclusive), not the strong-gradient bin.
std::array<std::uint32_t, 256> edgeWeightedHistogram(const std::vector<std::uint32_t>& gradientByLevel,
                                                     const std::array<std::uint32_t, 256>& gradientHisto,
                                                     int strongGradient) {
    int lowGradient = strongGradient * 33 / 100;
    if (lowGradient > 254) lowGradient = 254;
    int lastGradient = 255;
    for (int v = 0; v < 256; ++v) {
        if (gradientHisto[v] != 0) lastGradient = v;
    }

    std::array<std::uint32_t, 256> histo{};
    for (int g = lowGradient; g < lastGradient; ++g) {
        const std::uint32_t weight = static_cast<std::uint32_t>(g - lowGradient);
        for (int v = 0; v < 256; ++v) {
            histo[v] += weight * gradientByLevel[g * 256 + v];  // uint32 wrap, as legacy
        }
    }
    return histo;
}

// 0x4139d0: gaussian smoothing (float arithmetic, truncated per step), then
// threshold = centre of the span above peak/sqrt(2), tolerance = a third of
// the half-width of the span above peak/2.
AutoThreshold thresholdFromHistogram(const std::array<std::uint32_t, 256>& histo) {
    float weights[27];
    float weightSum = 0.0f;
    for (int k = -13; k <= 13; ++k) {
        const float exponent = static_cast<float>(-(k * k)) / 21.125f;
        weights[k + 13] = static_cast<float>(std::exp(static_cast<double>(exponent)));
        weightSum = weightSum + weights[k + 13];
    }

    std::array<std::uint32_t, 256> smooth = histo;
    for (int v = 13; v < 242; ++v) {
        std::uint32_t acc = 0;
        for (int k = -13; k <= 13; ++k) {
            const float product = static_cast<float>(histo[v + k]) * weights[k + 13];
            const float sum = static_cast<float>(acc) + product;
            acc = truncToU32(sum);
        }
        const float average = static_cast<float>(acc) / weightSum;
        smooth[v] = truncToU32(average);
    }

    std::uint32_t peak = 0;
    int threshold = 127;
    for (int v = 0; v < 255; ++v) {
        if (peak < smooth[v]) {
            peak = smooth[v];
            threshold = v;
        }
    }
    // An empty histogram: no edge stood out. The legacy defaults below stay,
    // for parity, but are flagged as meaningless.
    const bool found = peak > 0;

    const std::uint32_t level = truncToU32(static_cast<double>(peak) * 70.72135785007073 / 100.0);
    for (int v = 0; v < 256; ++v) {
        if (level < smooth[v]) {
            threshold = v;
            break;
        }
    }
    for (int v = 255; v >= 0; --v) {
        if (level < smooth[v]) {
            threshold = (v + threshold) / 2;
            break;
        }
    }

    const std::uint32_t half = peak >> 1;
    int width = 127;
    for (int v = 0; v < 256; ++v) {
        if (half < smooth[v]) {
            width = v;
            break;
        }
    }
    for (int v = 255; v >= 0; --v) {
        if (half < smooth[v]) {
            width = (v - width) / 2;
            break;
        }
    }

    smooth[threshold] = truncToU32(static_cast<double>(smooth[threshold]) * 0.7072135785007073);

    AutoThreshold result;
    result.threshold = threshold;
    result.tolerance = width / 3;  // denombre_metal_ima_fast divides by 3
    result.histogram = smooth;
    result.found = found;
    return result;
}

// Legacy stored each percentage as a float computed in double.
double legacyPercent(long long part, long long whole) {
    const double numerator = static_cast<double>(static_cast<float>(part)) * 100.0;
    return static_cast<float>(numerator / static_cast<double>(static_cast<float>(whole)));
}

const cv::Mat& diskKernel() {
    static const cv::Mat kernel = [] {
        cv::Mat k = cv::Mat::ones(5, 5, CV_8U);
        k.at<uchar>(0, 0) = k.at<uchar>(0, 4) = k.at<uchar>(4, 0) = k.at<uchar>(4, 4) = 0;
        return k;
    }();
    return kernel;
}

// 0x472400 (erosion) / 0x473420 (dilation): only the interior where the full
// 5x5 window fits is computed; the 2-pixel frame is then copied from the
// nearest computed row, then column (rows first, as legacy did).
cv::Mat legacyMorph(const cv::Mat& src, int op) {
    cv::Mat out;
    cv::morphologyEx(src, out, op, diskKernel(), cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
    const int rows = out.rows;
    const int cols = out.cols;
    out.row(2).copyTo(out.row(0));
    out.row(2).copyTo(out.row(1));
    out.row(rows - 3).copyTo(out.row(rows - 2));
    out.row(rows - 3).copyTo(out.row(rows - 1));
    out.col(2).copyTo(out.col(0));
    out.col(2).copyTo(out.col(1));
    out.col(cols - 3).copyTo(out.col(cols - 2));
    out.col(cols - 3).copyTo(out.col(cols - 1));
    return out;
}

} // namespace

cv::Mat legacyGrey(const cv::Mat& bgr) {
    if (bgr.type() == CV_8UC1) {
        return bgr.clone();
    }
    if (bgr.type() != CV_8UC3 && bgr.type() != CV_8UC4) {
        throw std::invalid_argument("legacyGrey: expected 8-bit grey, BGR or BGRA image");
    }
    const int channels = bgr.channels();
    cv::Mat grey(bgr.rows, bgr.cols, CV_8UC1);
    for (int r = 0; r < bgr.rows; ++r) {
        const uchar* in = bgr.ptr<uchar>(r);
        uchar* out = grey.ptr<uchar>(r);
        for (int c = 0; c < bgr.cols; ++c) {
            const uchar* px = in + c * channels;
            out[c] = static_cast<uchar>((px[0] + px[1] + px[2]) / 3);
        }
    }
    return grey;
}

AutoThreshold computeAutoThreshold(const cv::Mat& grey) {
    requireGrey8(grey, 3, "computeAutoThreshold");
    std::array<std::uint32_t, 256> gradientHisto{};
    std::vector<std::uint32_t> gradientByLevel(256 * 256, 0);
    gradientHistograms(grey, gradientHisto, gradientByLevel);
    const int strongGradient = strongGradientBin(gradientHisto);
    return thresholdFromHistogram(edgeWeightedHistogram(gradientByLevel, gradientHisto, strongGradient));
}

PhaseFractions countPhases(const cv::Mat& grey, int threshold, int tolerance) {
    requireGrey8(grey, 1, "countPhases");
    const int upper = threshold + tolerance;
    const int lower = threshold - tolerance;
    long long high = 0;
    long long band = 0;
    for (int r = 0; r < grey.rows; ++r) {
        const uchar* row = grey.ptr<uchar>(r);
        for (int c = 0; c < grey.cols; ++c) {
            const int p = row[c];
            if (upper < p) {
                ++high;
            } else if (lower <= p) {
                ++band;
            }
        }
    }
    const long long n = static_cast<long long>(grey.rows) * grey.cols;

    PhaseFractions f;
    f.darkPercent = legacyPercent(n - high - band / 2, n);
    f.lightPercent = legacyPercent(band / 2 + high, n);
    f.errorPercent = legacyPercent(band, n * 2);
    return f;
}

cv::Mat legacySmooth(const cv::Mat& grey) {
    requireGrey8(grey, 5, "legacySmooth");
    cv::Mat opened = legacyMorph(legacyMorph(grey, cv::MORPH_ERODE), cv::MORPH_DILATE);
    return legacyMorph(legacyMorph(opened, cv::MORPH_DILATE), cv::MORPH_ERODE);
}

PhaseAnalysis analyzePhases(const cv::Mat& image) {
    PhaseAnalysis analysis;
    if (image.type() == CV_8UC1) {
        analysis.analysedGrey = image.clone();
        analysis.threshold = computeAutoThreshold(analysis.analysedGrey);
    } else {
        const cv::Mat grey = legacyGrey(image);
        analysis.threshold = computeAutoThreshold(grey);
        analysis.analysedGrey = legacySmooth(grey);
    }
    analysis.fractions =
        countPhases(analysis.analysedGrey, analysis.threshold.threshold, analysis.threshold.tolerance);
    return analysis;
}

int minPhaseZoneSide(const cv::Size& size) {
    return static_cast<int>(std::ceil(kMinPhaseSide / legacyScale(size)));
}

std::optional<std::string> phaseZoneProblem(const cv::Rect& zone, const cv::Size& size) {
    const auto describe = [](const cv::Rect& r) {
        return std::to_string(r.width) + "x" + std::to_string(r.height) + " at (" + std::to_string(r.x) + ", " +
               std::to_string(r.y) + ")";
    };
    const std::string image = std::to_string(size.width) + "x" + std::to_string(size.height);
    if (zone.width <= 0 || zone.height <= 0 || (zone & cv::Rect(cv::Point(0, 0), size)) != zone) {
        return "the zone " + describe(zone) + " is not inside the " + image + " image";
    }
    const int side = minPhaseZoneSide(size);
    if (zone.width < side || zone.height < side) {
        return "the zone " + describe(zone) + " is too small to analyse: on a " + image + " image it needs at least " +
               std::to_string(side) + "x" + std::to_string(side) + " pixels (5x5 at the legacy scale)";
    }
    return std::nullopt;
}

PhaseAnalysis analyzeVuePhases(const cv::Mat& image, std::optional<cv::Rect> zone) {
    if (zone) {
        if (auto why = phaseZoneProblem(*zone, image.size())) {
            throw std::invalid_argument("analyzeVuePhases: " + *why);
        }
    }
    const cv::Mat part = zone ? image(*zone) : image;
    // The scale comes from the whole frame, so a zone sees the same detail
    // as the full photo it was cut from.
    const double scale = legacyScale(image.size());
    if (scale >= 1.0) return analyzePhases(part);
    cv::Mat reduced;
    cv::resize(part, reduced, cv::Size(), scale, scale, cv::INTER_AREA);
    return analyzePhases(reduced);
}

} // namespace msapp
