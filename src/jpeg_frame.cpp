#include "jpeg_frame.hpp"

namespace msapp {

JpegExtent findJpegExtent(const unsigned char* data, std::size_t size) {
    JpegExtent extent;
    if (data == nullptr || size < 2 || data[0] != 0xFF || data[1] != 0xD8) return extent;
    extent.startsWithSoi = true;
    std::size_t i = 2;
    while (i + 1 < size) {
        if (data[i] != 0xFF) {  // entropy-coded data after a start-of-scan header
            ++i;
            continue;
        }
        const unsigned char marker = data[i + 1];
        if (marker == 0xD9) {
            extent.end = i + 2;
            break;
        }
        if (marker == 0xFF) {  // fill byte: the next FF may start the marker
            ++i;
            continue;
        }
        // No length follows a stuffed zero in scan data (FF 00), TEM (FF 01),
        // a restart marker (FF D0-D7) or a stray start-of-image.
        if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) {
            i += 2;
            continue;
        }
        // Any other marker starts a segment whose big-endian length counts
        // itself but not the marker: skip the segment whole.
        if (i + 3 >= size) break;
        const std::size_t length = (static_cast<std::size_t>(data[i + 2]) << 8) | data[i + 3];
        i += 2 + length;
    }
    return extent;
}

} // namespace msapp
