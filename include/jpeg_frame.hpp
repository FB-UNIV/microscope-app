#pragma once

#include <cstddef>

namespace msapp {

// Where the JPEG image sits in a frame buffer read undecoded from an MJPG
// camera stream (CAP_PROP_CONVERT_RGB off, issue #76). A backend may hand
// over a fixed-size buffer, so the image can be followed by padding, even
// stale bytes of an earlier, larger frame. A buffer with no end-of-image
// marker is a truncated frame: libjpeg still decodes it, grey at the bottom.
struct JpegExtent {
    bool startsWithSoi = false;  // start-of-image marker FF D8 at byte 0
    std::size_t end = 0;         // one past the end-of-image marker FF D9; 0 when there is none

    bool complete() const { return startsWithSoi && end > 0; }
};

// Walks the JPEG segments from the start-of-image marker to the first
// end-of-image marker outside them, so an FF D9 inside a segment (an EXIF
// thumbnail) or in the padding after the image does not count.
JpegExtent findJpegExtent(const unsigned char* data, std::size_t size);

} // namespace msapp
