#include "format_microns.hpp"
#include <cmath>
#include <sstream>

namespace msapp {

std::string formatMicrons(double microns) {
    std::ostringstream oss;
    if (microns == std::floor(microns)) {
        oss << static_cast<long long>(microns);
    } else {
        oss << microns;
    }
    oss << " um";
    return oss.str();
}

} // namespace msapp
