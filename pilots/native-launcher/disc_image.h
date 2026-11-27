#ifndef XBOXLLR_DISC_IMAGE_H
#define XBOXLLR_DISC_IMAGE_H
#include <recomp_image/xdvdfs.hpp>
namespace xboxllr_disc {
inline std::vector<unsigned char> image(const char *path,uint64_t *source_size=nullptr) {
    const auto name=std::filesystem::u8path(path?path:"");
    auto extension=name.extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension==".iso" || extension==".xiso")
        return recomp_image::read_xdvdfs_root_file(path,"default.xbe",0x178,source_size);
    return recomp_image::read_bounded_file(path,source_size);
}
}
#endif
