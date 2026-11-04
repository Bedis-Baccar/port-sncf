#pragma once

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

/*
Minimal JSON-lines writer used to stream the solver's internal events
(column generation iterations, tree nodes, incumbents...) to trace.jsonl.
Every line is flushed so that the web app can follow a run live.
*/
class JsonObj {
public:
    JsonObj() { ss_ << std::setprecision(12) << '{'; }

    JsonObj& num(const char* k, double v) {
        key(k);
        // +-1e9 is the solver's "infinity": export it as null
        if (std::isfinite(v) && std::abs(v) < 1e9) ss_ << v; else ss_ << "null";
        return *this;
    }
    JsonObj& integer(const char* k, long long v) { key(k); ss_ << v; return *this; }
    JsonObj& boolean(const char* k, bool v) { key(k); ss_ << (v ? "true" : "false"); return *this; }
    JsonObj& str(const char* k, const std::string& v) { key(k); ss_ << '"' << escape(v) << '"'; return *this; }
    JsonObj& raw(const char* k, const std::string& json) { key(k); ss_ << json; return *this; }
    std::string dump() const { return ss_.str() + "}"; }

    static std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (static_cast<unsigned char>(c) < 0x20) continue;
            else out += c;
        }
        return out;
    }
    static std::string int_array(const std::vector<int>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) s += ',';
            s += std::to_string(v[i]);
        }
        return s + "]";
    }

private:
    void key(const char* k) {
        if (!first_) ss_ << ',';
        first_ = false;
        ss_ << '"' << k << "\":";
    }
    std::ostringstream ss_;
    bool first_ = true;
};

class Trace {
public:
    void open(const std::string& path) { out_.open(path); }
    bool enabled() const { return out_.is_open(); }
    void write(const JsonObj& obj) {
        if (!out_.is_open()) return;
        out_ << obj.dump() << '\n';
        out_.flush();
    }
    void close() { if (out_.is_open()) out_.close(); }

private:
    std::ofstream out_;
};
