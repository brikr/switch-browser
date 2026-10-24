#include "host/png.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace nxb {
namespace {

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = [] {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)init;
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24), v.push_back(x >> 16), v.push_back(x >> 8), v.push_back(x);
}

void chunk(std::FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> buf;
    put32(buf, static_cast<uint32_t>(data.size()));
    buf.insert(buf.end(), type, type + 4);
    buf.insert(buf.end(), data.begin(), data.end());
    put32(buf, crc32(buf.data() + 4, buf.size() - 4));
    std::fwrite(buf.data(), 1, buf.size(), f);
}

}  // namespace

bool write_png(const std::string& path, int width, int height, const uint8_t* rgba) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::fwrite(sig, 1, 8, f);

    std::vector<uint8_t> ihdr;
    put32(ihdr, width);
    put32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, no interlace
    chunk(f, "IHDR", ihdr);

    // Raw scanlines with filter byte 0, wrapped in zlib "stored" deflate blocks.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 4 + 1));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + static_cast<size_t>(y) * width * 4, rgba + static_cast<size_t>(y + 1) * width * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t off = 0; off < raw.size();) {
        size_t n = std::min<size_t>(65535, raw.size() - off);
        bool last = off + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xFF), z.push_back(n >> 8), z.push_back(~n & 0xFF), z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    uint32_t a = 1, b = 0;  // adler32
    for (uint8_t c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    put32(z, (b << 16) | a);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    return std::fclose(f) == 0;
}

void cairo_argb32_to_rgba(const uint32_t* src, int width, int height, int stride_px, uint8_t* dst) {
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            uint32_t p = src[static_cast<size_t>(y) * stride_px + x];
            uint8_t al = p >> 24, r = p >> 16, g = p >> 8, bl = p;
            if (al && al != 255) {  // un-premultiply
                r = static_cast<uint8_t>(std::min(255, r * 255 / al));
                g = static_cast<uint8_t>(std::min(255, g * 255 / al));
                bl = static_cast<uint8_t>(std::min(255, bl * 255 / al));
            }
            uint8_t* o = dst + (static_cast<size_t>(y) * width + x) * 4;
            o[0] = r, o[1] = g, o[2] = bl, o[3] = al;
        }
}

}  // namespace nxb
