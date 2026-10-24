#pragma once

#include <cstdint>
#include <string>

namespace nxb {

// Writes an RGBA8 image as PNG (uncompressed deflate; no zlib dependency). Returns false on I/O error.
bool write_png(const std::string& path, int width, int height, const uint8_t* rgba);

// Converts a cairo ARGB32 bitmap (premultiplied, native-endian words = B,G,R,A bytes on
// little-endian) to straight-alpha RGBA8.
void cairo_argb32_to_rgba(const uint32_t* src, int width, int height, int stride_px, uint8_t* dst);

}  // namespace nxb
