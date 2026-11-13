#include "rtti.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "globals.h"
#include "log.h"
#include "mem.h"

uint64_t L64(uintptr_t addr) {
    for (auto& s : g_snaps) {
        if (addr < s.base) continue;
        size_t off = addr - s.base;  
        if (off + 8 > s.size) continue;
        uint64_t v;
        memcpy(&v, s.buf.data() + off, 8);
        return v;
    }
    return 0;
}

std::string DemangleItanium(const std::string& m) {
    
    if (m.empty() || m[0] != 'N') return m;
    std::string out;
    size_t i = 1;
    while (i < m.size() && m[i] != 'E') {
        if (!isdigit((unsigned char)m[i])) return m;
        size_t j = i;
        while (j < m.size() && isdigit((unsigned char)m[j])) ++j;
        int len = atoi(m.substr(i, j - i).c_str());
        i = j;
        if (len <= 0 || i + (size_t)len > m.size()) return m;
        if (!out.empty()) out += "::";
        out += m.substr(i, len);
        i += len;
        if (i < m.size() && m[i] == 'I') {  
            int depth = 0;
            while (i < m.size()) {
                if (m[i] == 'I') ++depth;
                if (m[i] == 'E' && --depth == 0) {
                    ++i;
                    break;
                }
                ++i;
            }
        }
    }
    return out.empty() ? m : out;
}

std::string RttiGet(uintptr_t obj) {
    uint64_t vt = L64(obj);
    if (!vt) return "???";
    uint64_t ti = L64(vt - 16);
    if (!ti) return "???";
    uint64_t nm = L64(ti + 8);
    if (!nm || !g_rxActive) return "???";
    
    if (nm < g_rxActive || nm >= g_rxActive + 0x6aa8150) return "???";
    static FILE* f = nullptr;
    if (!f) {
        f = fopen(g_libPath.c_str(), "rb");
        if (!f) return "???";
    }
    fseek(f, (long)(nm - g_rxActive), SEEK_SET);
    char nb[224];
    size_t n = fread(nb, 1, sizeof(nb) - 1, f);
    nb[n] = 0;
    std::string raw(nb);
    if (raw.empty()) return "???";
    return DemangleItanium(raw);
}

bool SnapshotData() {
    g_snaps.clear();
    g_rxbases.clear();
    for (auto& m : ReadMaps()) {
        if (m.tag.find("/memfd") == std::string::npos) continue;
        if (m.tag[0] != 'r') continue;
        if (m.tag[2] == 'x') {
            if (m.e - m.s > (100ull << 20) && m.e - m.s < (120ull << 20))
                g_rxbases.push_back(m.s);
            continue;
        }
        
        Snap s;
        s.base = m.s;
        s.size = m.e - m.s;
        s.buf.resize(s.size);
        size_t off = 0;
        const size_t CH = 2 << 20;
        while (off < s.size) {
            size_t n = std::min(CH, s.size - off);
            if (!ReadBytes(s.base + off, s.buf.data() + off, n)) break;
            off += n;
        }
        if (off == 0) continue;
        s.buf.resize(off);
        s.size = off;
        g_snaps.push_back(std::move(s));
    }
    if (g_snaps.empty() || g_rxbases.empty()) return false;
    
    size_t bestN = 0;
    for (uintptr_t b : g_rxbases) {
        size_t cnt = 0;
        for (auto& s : g_snaps)
            for (size_t i = 0; i + 8 <= s.buf.size(); i += 8) {
                uint64_t v;
                memcpy(&v, s.buf.data() + i, 8);
                if (v >= b && v < b + 0x6aa8150 && ++cnt > 200000) break;
            }
        if (cnt > bestN) {
            bestN = cnt;
            g_rxActive = b;
        }
    }
    LogInfo("[snap] data ranges: %zu rx candidates: %zu active rx=%#lx", g_snaps.size(), g_rxbases.size(), g_rxActive);
    return g_rxActive != 0;
}

uintptr_t ActiveRx() {
    
    uintptr_t relro = 0;
    uintptr_t best = 0, bestEnd = 0;
    for (auto& m : ReadMaps()) {
        if (m.tag.find("/memfd") == std::string::npos) continue;
        if (m.tag[1] == '-' && m.tag[2] == '-' && m.e - m.s > (1ull << 20))
            relro = m.s;
        if (m.tag[2] == 'x' && m.e - m.s > (100ull << 20)) {
            if (m.e > bestEnd) {
                bestEnd = m.e;
                best = m.s;
            }
        }
    }
    if (relro) {
        
        uintptr_t cand = 0;
        for (auto& m : ReadMaps()) {
            if (m.tag.find("/memfd") == std::string::npos) continue;
            if (!(m.tag[2] == 'x' && m.e - m.s > (100ull << 20))) continue;
            if (m.e <= relro && m.e > cand) cand = m.s;
        }
        if (cand) return cand;
    }
    return best;
}
