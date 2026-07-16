// xlle — minimal JSONL emitter (PRINCIPLES §7: structured data only, one event per line)
//
// Dependency-free on purpose: the observability schema is load-bearing and we do not
// want a third-party JSON lib on the hot path or in the wire format this early. One
// object per line; the engine, the xemu bridge, and the hardware probe all speak it.
#pragma once
#include <cstdint>
#include <sstream>
#include <string>

namespace xlle {

// Builds a single JSON object line: {"k":v,...}\n . Values are appended as typed
// fields. Not a general JSON library — just enough for flat event records.
class JsonLine {
public:
    JsonLine& str(const char* key, const std::string& v) {
        sep();
        os_ << '"' << key << "\":\"";
        escape(v);
        os_ << '"';
        return *this;
    }
    JsonLine& i(const char* key, long long v) {
        sep();
        os_ << '"' << key << "\":" << v;
        return *this;
    }
    JsonLine& u(const char* key, unsigned long long v) {
        sep();
        os_ << '"' << key << "\":" << v;
        return *this;
    }
    // Hex is the natural form for addresses/registers; emitted as a JSON string.
    JsonLine& hex(const char* key, unsigned long long v) {
        sep();
        os_ << '"' << key << "\":\"0x" << std::hex << v << std::dec << '"';
        return *this;
    }
    JsonLine& b(const char* key, bool v) {
        sep();
        os_ << '"' << key << "\":" << (v ? "true" : "false");
        return *this;
    }

    // Returns the object with a trailing newline (JSONL).
    std::string line() const { return "{" + os_.str() + "}\n"; }
    std::string obj() const { return "{" + os_.str() + "}"; }

private:
    void sep() {
        if (!first_) os_ << ',';
        first_ = false;
    }
    void escape(const std::string& s) {
        for (char c : s) {
            switch (c) {
                case '"': os_ << "\\\""; break;
                case '\\': os_ << "\\\\"; break;
                case '\n': os_ << "\\n"; break;
                case '\r': os_ << "\\r"; break;
                case '\t': os_ << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        os_ << buf;
                    } else {
                        os_ << c;
                    }
            }
        }
    }
    std::ostringstream os_;
    bool first_ = true;
};

}  // namespace xlle
