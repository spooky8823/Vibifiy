#pragma once
// Pure (platform independent) helpers for the Windows .ico container and the
// RT_GROUP_ICON resource that mirrors it. Shared by the browser (writes the
// accent-coloured .ico) and by vibrium_updater (embeds it into vibrium.exe).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vibrium::ico {

struct Image {
    int width = 0;                 // real pixel size (256 for the 256px entry)
    int height = 0;
    uint16_t planes = 1;
    uint16_t bitCount = 32;
    std::vector<uint8_t> data;     // PNG stream, or DIB (header + pixels + mask)
};

inline void Put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
inline void Put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF));
}
inline uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Icon image payload for sizes below 256: BITMAPINFOHEADER (height doubled, as
// icons require), bottom-up 32-bit BGRA pixels, then a 1-bit AND mask. The
// alpha channel carries the transparency, so the mask is all zero.
inline std::vector<uint8_t> MakeDib(int size, const uint8_t* bgraTopDown) {
    const uint32_t maskStride = static_cast<uint32_t>(((size + 31) / 32) * 4);
    const uint32_t pixelBytes = static_cast<uint32_t>(size) * size * 4;
    std::vector<uint8_t> out;
    out.reserve(40 + pixelBytes + maskStride * size);
    Put32(out, 40);                                   // biSize
    Put32(out, static_cast<uint32_t>(size));          // biWidth
    Put32(out, static_cast<uint32_t>(size * 2));      // biHeight (XOR + AND)
    Put16(out, 1);                                    // biPlanes
    Put16(out, 32);                                   // biBitCount
    Put32(out, 0);                                    // biCompression = BI_RGB
    Put32(out, pixelBytes + maskStride * size);       // biSizeImage
    Put32(out, 0); Put32(out, 0);                     // pixels per metre
    Put32(out, 0); Put32(out, 0);                     // colours used / important
    for (int y = size - 1; y >= 0; --y) {
        const uint8_t* row = bgraTopDown + static_cast<size_t>(y) * size * 4;
        out.insert(out.end(), row, row + static_cast<size_t>(size) * 4);
    }
    out.insert(out.end(), static_cast<size_t>(maskStride) * size, 0);
    return out;
}

inline std::vector<uint8_t> BuildIco(const std::vector<Image>& images) {
    std::vector<uint8_t> out;
    Put16(out, 0);                                    // reserved
    Put16(out, 1);                                    // type: icon
    Put16(out, static_cast<uint16_t>(images.size()));
    uint32_t offset = static_cast<uint32_t>(6 + 16 * images.size());
    for (const auto& im : images) {
        out.push_back(im.width >= 256 ? 0 : static_cast<uint8_t>(im.width));
        out.push_back(im.height >= 256 ? 0 : static_cast<uint8_t>(im.height));
        out.push_back(0);                             // palette colours
        out.push_back(0);                             // reserved
        Put16(out, im.planes);
        Put16(out, im.bitCount);
        Put32(out, static_cast<uint32_t>(im.data.size()));
        Put32(out, offset);
        offset += static_cast<uint32_t>(im.data.size());
    }
    for (const auto& im : images) out.insert(out.end(), im.data.begin(), im.data.end());
    return out;
}

inline bool ParseIco(const uint8_t* p, size_t n, std::vector<Image>& out) {
    out.clear();
    if (n < 6 || Get16(p) != 0 || Get16(p + 2) != 1) return false;
    const size_t count = Get16(p + 4);
    if (count == 0 || n < 6 + 16 * count) return false;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* e = p + 6 + 16 * i;
        Image im;
        im.width = e[0] == 0 ? 256 : e[0];
        im.height = e[1] == 0 ? 256 : e[1];
        im.planes = Get16(e + 4);
        im.bitCount = Get16(e + 6);
        const uint32_t bytes = Get32(e + 8), offset = Get32(e + 12);
        if (offset > n || bytes > n - offset) return false;
        im.data.assign(p + offset, p + offset + bytes);
        out.push_back(std::move(im));
    }
    return true;
}

// RT_GROUP_ICON payload. Entry ids refer to the RT_ICON resources
// firstId, firstId + 1, ... that hold each image's data.
inline std::vector<uint8_t> BuildGroupIcon(const std::vector<Image>& images, uint16_t firstId) {
    std::vector<uint8_t> out;
    Put16(out, 0);
    Put16(out, 1);
    Put16(out, static_cast<uint16_t>(images.size()));
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& im = images[i];
        out.push_back(im.width >= 256 ? 0 : static_cast<uint8_t>(im.width));
        out.push_back(im.height >= 256 ? 0 : static_cast<uint8_t>(im.height));
        out.push_back(0);
        out.push_back(0);
        Put16(out, im.planes);
        Put16(out, im.bitCount);
        Put32(out, static_cast<uint32_t>(im.data.size()));
        Put16(out, static_cast<uint16_t>(firstId + i));
    }
    return out;
}

// Ids of the RT_ICON resources an existing RT_GROUP_ICON points at.
inline bool ParseGroupIconIds(const uint8_t* p, size_t n, std::vector<uint16_t>& ids) {
    ids.clear();
    if (n < 6 || Get16(p) != 0 || Get16(p + 2) != 1) return false;
    const size_t count = Get16(p + 4);
    if (n < 6 + 14 * count) return false;
    for (size_t i = 0; i < count; ++i) ids.push_back(Get16(p + 6 + 14 * i + 12));
    return true;
}

}  // namespace vibrium::ico