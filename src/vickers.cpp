#include "vickers.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace msapp {

std::string VickersResult::label() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << hardness << " HV";
    std::ostringstream load;
    load << loadKgf;
    oss << load.str();
    return oss.str();
}

VickersResult vickersHardness(double diagonal1Microns, double diagonal2Microns, double loadKgf) {
    if (diagonal1Microns <= 0.0 || diagonal2Microns <= 0.0) {
        throw std::invalid_argument("vickersHardness: both diagonals must be > 0 um");
    }
    if (loadKgf <= 0.0) {
        throw std::invalid_argument("vickersHardness: load must be > 0 kgf");
    }

    VickersResult r;
    r.diagonal1Microns = diagonal1Microns;
    r.diagonal2Microns = diagonal2Microns;
    r.meanDiagonalMicrons = (diagonal1Microns + diagonal2Microns) / 2.0;
    r.loadKgf = loadKgf;
    const double meanMm = r.meanDiagonalMicrons / 1000.0;
    r.hardness = kLegacyVickersCoefficient * loadKgf / (meanMm * meanMm);
    return r;
}

VickersResult vickersHardness(const Measurement& diagonal1, const Measurement& diagonal2, double loadKgf) {
    return vickersHardness(diagonal1.realDistanceMicrons(), diagonal2.realDistanceMicrons(), loadKgf);
}

} // namespace msapp
