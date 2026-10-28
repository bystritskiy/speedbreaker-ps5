// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See mem_scan.h.
//
// Commands (one per write of scan/cmd.txt; addresses are guest, hex):
//   new <f32|u32|u16|u8> <min> <max> [from to]   values in [min, max]
//   filter <min> <max>                           keep those in range now
//   changed | same | up | down                   against the last look
//   list [n]                                     the candidates and values
//   near <addr> <bytes>                          a region as u32 and f32
//   watch <type>:<addr>[,...] <hz> <seconds>     a CSV of values over time
// The guest is big-endian; values are compared as numbers. Pages not
// resident on the host are skipped (reading them would commit them); the PS5
// commits all of guest memory up front, so there it reads everything.
#include <stdafx.h>
#include "mem_scan.h"
#include <kernel/memory.h>
#include <user/paths.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#ifndef __PROSPERO__
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
    enum class Type { F32, U32, U16, U8 };
    int Size(Type t) { return t == Type::U8 ? 1 : t == Type::U16 ? 2 : 4; }

    bool ParseType(const std::string& s, Type& t)
    {
        if (s == "f32") t = Type::F32;
        else if (s == "u32") t = Type::U32;
        else if (s == "u16") t = Type::U16;
        else if (s == "u8") t = Type::U8;
        else return false;
        return true;
    }

    double Read(Type t, uint32_t addr)
    {
        const uint8_t* p = g_memory.base + addr;
        switch (t)
        {
        case Type::U8: return p[0];
        case Type::U16: return double(uint16_t(p[0] << 8 | p[1]));
        case Type::U32: return double(uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]);
        case Type::F32:
        {
            uint32_t bits = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
            float f;
            memcpy(&f, &bits, 4);
            return f != f ? 1e30 : f;  // NaN never matches
        }
        }
        return 0;
    }

    struct Cand
    {
        uint32_t addr;
        double last;
    };

    std::string s_dir;
    Type s_type = Type::F32;
    std::vector<Cand> s_cands;
    constexpr size_t kMaxCands = 30'000'000;

    // Calls fn(start, end) for each run of resident host pages in [from, to).
    template <class Fn> void ResidentRuns(uint64_t from, uint64_t to, Fn fn)
    {
#ifdef __PROSPERO__
        fn(from, to);
#else
        const uint64_t page = uint64_t(getpagesize());
        constexpr uint64_t kChunk = 64ull << 20;
        std::vector<char> vec(kChunk / page);
        for (uint64_t c = from & ~(page - 1); c < to; c += kChunk)
        {
            const uint64_t len = std::min(kChunk, to - c);
            if (mincore(g_memory.base + c, len, vec.data()) != 0)
                continue;
            for (uint64_t i = 0; i < (len + page - 1) / page; i++)
                if (vec[i] & 1)
                {
                    uint64_t s = c + i * page, e = s + page;
                    while (i + 1 < (len + page - 1) / page && (vec[i + 1] & 1))
                        i++, e += page;
                    fn(std::max(s, from), std::min(e, to));
                }
        }
#endif
    }

    std::string NewScan(Type t, double lo, double hi, uint64_t from, uint64_t to)
    {
        s_type = t;
        s_cands.clear();
        const int step = Size(t);
        bool capped = false;
        ResidentRuns(from, to, [&](uint64_t s, uint64_t e) {
            for (uint64_t a = (s + step - 1) / step * step; a + step <= e && !capped; a += step)
            {
                double v = Read(t, uint32_t(a));
                if (v >= lo && v <= hi)
                {
                    s_cands.push_back({ uint32_t(a), v });
                    capped = s_cands.size() >= kMaxCands;
                }
            }
        });
        return std::to_string(s_cands.size()) + " candidates" + (capped ? " (capped: narrow the range)" : "");
    }

    template <class Keep> std::string Filter(Keep keep)
    {
        std::vector<Cand> out;
        for (const Cand& c : s_cands)
        {
            double v = Read(s_type, c.addr);
            if (keep(c.last, v))
                out.push_back({ c.addr, v });
        }
        s_cands.swap(out);
        return std::to_string(s_cands.size()) + " candidates";
    }

    std::string List(size_t n)
    {
        std::ostringstream o;
        o << s_cands.size() << " candidates\n";
        char line[96];
        for (size_t i = 0; i < s_cands.size() && i < n; i++)
        {
            snprintf(line, sizeof line, "%08X %.4f\n", s_cands[i].addr, Read(s_type, s_cands[i].addr));
            o << line;
        }
        return o.str();
    }

    std::string Near(uint32_t addr, uint32_t bytes)
    {
        std::ostringstream o;
        char line[128];
        const uint32_t start = (addr - std::min(addr, bytes)) & ~3u;
        for (uint32_t a = start; a < addr + bytes; a += 4)
        {
#ifndef __PROSPERO__
            unsigned char vec = 0;
            const uint64_t page = uint64_t(getpagesize());
            if (mincore(g_memory.base + (a & ~(page - 1)), page, (char*)&vec) != 0 || !(vec & 1))
                continue;
#endif
            snprintf(line, sizeof line, "%08X %+6d  %08X  %14.4f\n", a, int(a) - int(addr), uint32_t(Read(Type::U32, a)), Read(Type::F32, a));
            o << line;
        }
        return o.str();
    }

    std::string Watch(const std::string& spec, double hz, double seconds)
    {
        std::vector<std::pair<Type, uint32_t>> items;
        std::stringstream ss(spec);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            auto colon = item.find(':');
            Type t;
            if (colon == std::string::npos || !ParseType(item.substr(0, colon), t))
                return "bad watch item: " + item;
            items.push_back({ t, uint32_t(strtoul(item.c_str() + colon + 1, nullptr, 16)) });
        }
        std::ostringstream o;
        o << "t";
        for (auto& [t, a] : items)
        {
            char h[16];
            snprintf(h, sizeof h, ",%08X", a);
            o << h;
        }
        o << "\n";
        const auto start = std::chrono::steady_clock::now();
        const auto period = std::chrono::duration<double>(1.0 / std::max(hz, 1.0));
        for (int n = 0;; n++)
        {
            const auto due = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(period * n);
            if (due - start > std::chrono::duration<double>(seconds))
                break;
            std::this_thread::sleep_until(due);
            char v[48];
            snprintf(v, sizeof v, "%.2f", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            o << v;
            for (auto& [t, a] : items)
            {
                snprintf(v, sizeof v, ",%.4f", Read(t, a));
                o << v;
            }
            o << "\n";
        }
        return o.str();
    }

    std::string Run(const std::string& cmdline)
    {
        std::istringstream in(cmdline);
        std::string cmd;
        in >> cmd;
        if (cmd == "new")
        {
            std::string ts;
            double lo, hi;
            std::string from = "10000", to = "C0000000";
            Type t;
            if (!(in >> ts >> lo >> hi) || !ParseType(ts, t))
                return "usage: new <f32|u32|u16|u8> <min> <max> [from to]";
            in >> from >> to;
            return NewScan(t, lo, hi, strtoull(from.c_str(), nullptr, 16), strtoull(to.c_str(), nullptr, 16));
        }
        if (cmd == "filter")
        {
            double lo, hi;
            if (!(in >> lo >> hi))
                return "usage: filter <min> <max>";
            return Filter([&](double, double v) { return v >= lo && v <= hi; });
        }
        if (cmd == "changed") return Filter([](double a, double b) { return a != b; });
        if (cmd == "same") return Filter([](double a, double b) { return a == b; });
        if (cmd == "up") return Filter([](double a, double b) { return b > a; });
        if (cmd == "down") return Filter([](double a, double b) { return b < a; });
        if (cmd == "list")
        {
            size_t n = 100;
            in >> n;
            return List(n);
        }
        if (cmd == "near")
        {
            std::string a;
            uint32_t bytes = 256;
            if (!(in >> a))
                return "usage: near <addr> <bytes>";
            in >> bytes;
            return Near(uint32_t(strtoul(a.c_str(), nullptr, 16)), bytes);
        }
        if (cmd == "watch")
        {
            std::string spec;
            double hz = 10, seconds = 10;
            if (!(in >> spec))
                return "usage: watch <type>:<addr>[,...] <hz> <seconds>";
            in >> hz >> seconds;
            return Watch(spec, hz, std::min(seconds, 120.0));
        }
        return "unknown command: " + cmd;
    }

    void Loop()
    {
        const std::string cmdPath = s_dir + "/cmd.txt", outPath = s_dir + "/out.txt";
        long long seen = -1;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            struct stat st;
            if (stat(cmdPath.c_str(), &st) != 0)
                continue;
            // (seconds and size: st_mtim and st_mtimespec are spelled per OS)
            const long long stamp = (long long)st.st_mtime * 1000003 + (long long)st.st_size;
            if (stamp == seen)
                continue;
            seen = stamp;
            std::ifstream f(cmdPath);
            std::string line;
            std::getline(f, line);
            const auto t0 = std::chrono::steady_clock::now();
            std::string result = Run(line);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "[memscan] %s -> %.0f ms\n", line.c_str(), ms);
            std::ofstream out(outPath + ".tmp");
            out << "> " << line << "\n" << result << "\n";
            out.close();
            rename((outPath + ".tmp").c_str(), outPath.c_str());
        }
    }
}

namespace memscan
{
    void Start()
    {
        if (!g_memory.base)
            return;
        s_dir = (GetUserPath() / "scan").string();
        struct stat st;
        if (stat(s_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            return;
        fprintf(stderr, "[memscan] on: commands in %s/cmd.txt\n", s_dir.c_str());
        std::thread(Loop).detach();
    }
}
