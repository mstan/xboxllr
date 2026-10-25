/* Bounded streaming gzip I/O. The decompressed journal retains its own
 * identity, record CRCs and exact sequence checks. Gzip checks the transport
 * too; truncated or corrupt streams never become a clean end of journal. */
#pragma once
#include <zlib.h>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <cstdio>
#include <stdexcept>

namespace pilot_session_journal {
inline gzFile open(const std::filesystem::path& path,const char *mode) {
#ifdef _WIN32
    return gzopen_w(path.c_str(),mode);
#else
    return gzopen(path.c_str(),mode);
#endif
}
class Output {
    gzFile file=nullptr;
    std::filesystem::path path;
    bool bad=false;
public:
    ~Output(){if(file)gzclose(file);}
    void open(const std::filesystem::path& destination) {
        path=destination;file=pilot_session_journal::open(path,"wb1x");
        if(!file || gzbuffer(file,65536)!=0)bad=true;
    }
    bool is_open()const{return file!=nullptr;}
    void write(const char *bytes,std::streamsize size) {
        if(!file || size<0 || (uint64_t)size>UINT_MAX ||
           gzwrite(file,bytes,(unsigned)size)!=size)bad=true;
    }
    void flush(){if(!file || gzflush(file,Z_SYNC_FLUSH)!=Z_OK)bad=true;}
    uint64_t offset()const {
        return std::filesystem::file_size(path);
    }
    void close(){if(file){if(gzclose(file)!=Z_OK)bad=true;file=nullptr;}}
    bool operator!()const{return bad;}
};
class Input {
    gzFile file=nullptr;
    std::streamsize count=0;
    bool bad=false;
    bool error()const {
        int code=Z_OK;gzerror(file,&code);
        return code!=Z_OK && code!=Z_STREAM_END;
    }
public:
    ~Input(){if(file)gzclose(file);}
    void open(const std::filesystem::path& path) {
        file=pilot_session_journal::open(path,"rbG"); // Require gzip; no transparent raw input.
        if(!file || gzbuffer(file,65536)!=0)bad=true;
    }
    void read(char *bytes,std::streamsize size) {
        if(!file || size<0 || (uint64_t)size>INT_MAX){bad=true;count=0;return;}
        int result=gzread(file,bytes,(unsigned)size);
        count=result<0?0:result;if(result<0 || error())bad=true;
    }
    std::streamsize gcount()const{return count;}
    bool eof()const{return file && !bad && gzeof(file)!=0;}
    int peek() {
        unsigned char byte=0;read((char*)&byte,1);
        if(bad)throw std::runtime_error("compressed journal read/checksum failed");
        if(!count)return EOF;
        if(gzungetc(byte,file)<0)throw std::runtime_error("compressed journal lookahead failed");
        return byte;
    }
    bool operator!()const{return !file || bad;}
};
}
