/* Public certificate fields only. This never approves an executable hash. */
#include "xbe_identity.h"
#include "disc_image.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

XboxllrXbeIdentity xboxllr_xbe_identity = {};
uint32_t xboxllr_expected_title_id = 0;
int xboxllr_has_expected_title_id = 0;

static uint32_t word(const unsigned char *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static unsigned unit(const unsigned char *p) { return p[0] | unsigned(p[1]) << 8; }
static void append(std::string &name, unsigned cp) {
    // Match the Python reader's single-line display policy, including bidi controls.
    if (cp < 32 || (cp >= 0x7f && cp <= 0x9f) || cp == 0xad || cp == 0x61c || cp == 0x180e ||
        (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e) ||
        (cp >= 0x2060 && cp <= 0x206f) || cp == 0xfeff || (cp >= 0xfff9 && cp <= 0xfffb) ||
        (cp >= 0xe0000 && cp <= 0xe007f)) cp = ' ';
    if (cp < 0x80) name += char(cp);
    else if (cp < 0x800) {name += char(0xc0 | cp >> 6);name += char(0x80 | (cp & 63));}
    else if (cp < 0x10000) {
        name += char(0xe0 | cp >> 12);name += char(0x80 | ((cp >> 6) & 63));name += char(0x80 | (cp & 63));
    } else {
        name += char(0xf0 | cp >> 18);name += char(0x80 | ((cp >> 12) & 63));
        name += char(0x80 | ((cp >> 6) & 63));name += char(0x80 | (cp & 63));
    }
}
XboxllrXbeIdentity xboxllr_read_xbe_identity(const char *path) {
    XboxllrXbeIdentity result = {};
    try {
        const auto bytes=xboxllr_disc::image(path);
        const auto length=bytes.size();const auto *header=bytes.data();
        if (length<0x178 || std::memcmp(header, "XBEH", 4))
            throw std::runtime_error("Not an XBE image");
        const uint32_t base = word(header + 0x104), headers = word(header + 0x108), va = word(header + 0x118);
        const uint64_t cert = uint64_t(va) - base;
        if (headers < 0x178 || uint64_t(headers) > uint64_t(length) ||
            va < base || cert < 0x178 || cert + 0x5c > headers)
            throw std::runtime_error("XBE certificate identity is outside headers");
        const auto *public_fields=bytes.data()+cert;
        const uint32_t size = word(public_fields);
        if (size < 0x5c || cert + size > headers)
            throw std::runtime_error("XBE certificate extent is invalid");
        result.title_id = word(public_fields + 8);
        std::string name;
        for (unsigned i = 0; i < 40; ++i) {
            unsigned cp = unit(public_fields + 12 + i * 2);
            if (!cp) break;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                const unsigned low = i + 1 < 40 ? unit(public_fields + 12 + (i + 1) * 2) : 0;
                if (low >= 0xdc00 && low <= 0xdfff) {cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;++i;}
                else cp = 0xfffd;
            } else if (cp >= 0xdc00 && cp <= 0xdfff) cp = 0xfffd;
            append(name, cp);
        }
        const auto first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
        name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
        std::snprintf(result.title_name, sizeof(result.title_name), "%s", name.c_str());
        const unsigned a = result.title_id >> 24, b = (result.title_id >> 16) & 255;
        if (a >= 'A' && a <= 'Z' && b >= 'A' && b <= 'Z')
            std::snprintf(result.title_label, sizeof(result.title_label), "%c%c-%03u (0x%08X)", a, b,
                          result.title_id & 0xffff, result.title_id);
        else std::snprintf(result.title_label, sizeof(result.title_label), "0x%08X", result.title_id);
        result.valid = 1;
    } catch (const std::exception &error) {
        std::snprintf(result.error, sizeof(result.error), "%s", error.what());
    }
    return result;
}
void xboxllr_xbe_select(const char *path) { xboxllr_xbe_identity = xboxllr_read_xbe_identity(path); }
int xboxllr_read_game_image(const char *path,unsigned char **data,size_t *size,uint64_t *source_size) {
    if(!data || !size || !source_size)return 0;
    *data=nullptr;*size=0;*source_size=0;
    try {
        const auto bytes=xboxllr_disc::image(path,source_size);
        auto *copy=static_cast<unsigned char*>(std::malloc(bytes.size()));
        if(!copy)return 0;
        std::memcpy(copy,bytes.data(),bytes.size());*data=copy;*size=bytes.size();return 1;
    } catch(const std::exception&){return 0;}
}
const char *xboxllr_xbe_rejection(void) {
    if (!xboxllr_xbe_identity.valid) return xboxllr_xbe_identity.error;
    if (xboxllr_has_expected_title_id && xboxllr_xbe_identity.title_id != xboxllr_expected_title_id)
        return "Wrong game selected";
    return "Unsupported XBE revision";
}
