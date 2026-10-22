// Metadata-only admission and explicit disposable-fixture qualification CLI.
#include "fatx_volume.hpp"
#include <iostream>
#include <cstring>
int main(int argc,char** argv) {
    try {
        if(argc!=3 || (std::strcmp(argv[1],"inspect") && std::strcmp(argv[1],"fixture-write")))return 2;
        bool mutate=!std::strcmp(argv[1],"fixture-write");
        xllr::storage::XboxDisk disk(argv[2],mutate);
        std::cout<<(disk.image.qcow2()?"qcow2":"raw")<<" "<<disk.image.size()<<"\n";
        for(unsigned i=1;i<=5;++i) {
            auto& fs=*disk.partitions[i];
            std::cout<<i<<" "<<fs.cluster_size()<<" "<<fs.clusters()<<" "<<fs.free_clusters()<<"\n";
        }
        if(mutate) {
            auto& fs=*disk.partitions[1];
            auto dir=fs.create("xllr-fixture",true,0);
            auto file=fs.create("xllr-fixture\\payload.dat",false,0);
            const char payload[]="Actual FATX persistence from xboxllr";
            fs.write(file,71,payload,sizeof(payload));
            char readback[sizeof(payload)];
            if(fs.read(file,71,readback,sizeof(readback))!=sizeof(payload) || std::memcmp(payload,readback,sizeof(payload)))return 3;
            fs.rename(file,"xllr-fixture\\renamed.dat",false);
            disk.image.flush();
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
