#include "dump.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "globals.h"
#include "log.h"
#include "mem.h"
#include "roblox.h"
#include "rtti.h"
#include "scanner.h"

namespace Dump {





static bool ReadFileBytes(std::vector<char>& out) {
    FILE* f = fopen(g_dumpedPath.c_str(), "rb");
    if (!f) return false;
    char b[1 << 20];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0)
        out.insert(out.end(), b, b + n);
    fclose(f);
    return !out.empty();
}

inline bool ModRmRip(unsigned char m) { return (m & 0xC7) == 0x05; }


bool LeaRdi(const std::vector<char>& d, size_t pos, uint64_t& target) {
    size_t size = d.size();
    auto u8 = [&](size_t i) -> unsigned char {
        return static_cast<unsigned char>(d[i]);
    };
    if (pos + 7 <= size && u8(pos) >= 0x48 && u8(pos) <= 0x4C &&
        u8(pos + 1) == 0x8D && u8(pos + 2) == 0x3D) {
        int32_t disp;
        memcpy(&disp, &d[pos + 3], 4);
        target = pos + 7 + (int64_t)disp;
        return true;
    }
    if (pos + 6 <= size && u8(pos) == 0x8D && u8(pos + 1) == 0x3D) {
        int32_t disp;
        memcpy(&disp, &d[pos + 2], 4);
        target = pos + 6 + (int64_t)disp;
        return true;
    }
    return false;
}


std::vector<uint64_t> FindStrings(const std::vector<char>& d,
                                  const char* name) {
    std::vector<uint64_t> out;
    size_t nl = strlen(name);
    size_t off = 0;
    while (off + nl + 1 <= d.size()) {
        const void* p = memmem(d.data() + off, d.size() - off, name, nl);
        if (!p) break;
        size_t s = (const char*)p - d.data();
        off = s + 1;
        if (d[s + nl] != 0) continue;
        if (s > 0) {
            unsigned char pre = d[s - 1];
            if ((pre >= 'A' && pre <= 'Z') || (pre >= 'a' && pre <= 'z') ||
                (pre >= '0' && pre <= '9') || pre == '_')
                continue;
        }
        out.push_back(s);
        if (out.size() >= 16) break;
    }
    return out;
}



std::vector<uint64_t> FindSlots(const std::vector<char>& d, uint64_t strVA) {
    std::vector<uint64_t> sites;
    size_t size = d.size();
    auto u8 = [&](size_t i) -> unsigned char {
        return static_cast<unsigned char>(d[i]);
    };
    for (size_t i = 0; i + 7 < size; ++i) {
        bool hit = false;
        if (u8(i) >= 0x48 && u8(i) <= 0x4F &&
            (u8(i + 1) == 0x8D || u8(i + 1) == 0x8B ||
             u8(i + 1) == 0x89) &&
            ModRmRip(u8(i + 2))) {
            int32_t disp;
            memcpy(&disp, &d[i + 3], 4);
            if ((uint64_t)(i + 7 + (int64_t)disp) == strVA) hit = true;
        } else if ((u8(i) == 0x8D || u8(i) == 0x8B || u8(i) == 0x89) &&
                   ModRmRip(u8(i + 1))) {
            int32_t disp;
            memcpy(&disp, &d[i + 2], 4);
            if ((uint64_t)(i + 6 + (int64_t)disp) == strVA) hit = true;
        }
        if (hit) {
            sites.push_back(i);
            i += 6;
        }
    }
    std::vector<uint64_t> slots;
    for (uint64_t s : sites) {
        size_t lo = s > 64 ? s - 64 : 0;
        for (size_t k = lo; k + 7 < s + 24 && k < size; ++k) {
            uint64_t tgt = 0;
            if (LeaRdi(d, k, tgt)) slots.push_back(tgt);
        }
    }
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    return slots;
}

bool World() {
    if (!g_rxActive) {
        g_rxActive = ActiveRx();
        if (!g_rxActive) {
            LogBad("World FAILED: no rx");
            return false;
        }
    }
    uintptr_t rx = g_rxActive;
    uintptr_t t0 = 0, tsz = 0;
    TextFileRange(t0, tsz);
    uintptr_t tEnd = rx + tsz;
    auto inText = [&](uint64_t a) {
        return tsz != 0 && a >= rx && a < tEnd;
    };
    std::vector<char> file;
    if (!ReadFileBytes(file)) {
        LogBad("World FAILED: cannot read reference binary");
        return false;
    }
    const uintptr_t kF[] = {0x80, 0x78, 0x88, 0x70, 0x90, 0x68, 0x98};
    const uintptr_t kFx[] = {0x60, 0x58, 0x50, 0x48, 0xA0,
                             0xA8, 0xB0, 0xB8, 0xC0};
    
    
    auto findMethod = [&](const char* name, bool verbose) {
        std::vector<std::pair<uintptr_t, uintptr_t>> out;  
        for (uint64_t sva : FindStrings(file, name)) {
            for (uint64_t slot : FindSlots(file, sva)) {
                for (uintptr_t fo : kF) {
                    uintptr_t fn = Read<uintptr_t>(rx + slot + fo);
                    if (!inText(fn)) continue;
                    out.emplace_back(slot, fo);
                    goto nextslot;
                }
                for (uintptr_t fo : kFx) {
                    uintptr_t fn = Read<uintptr_t>(rx + slot + fo);
                    if (!inText(fn)) continue;
                    out.emplace_back(slot, fo);
                    goto nextslot;
                }
            nextslot:;
            }
        }
        if (verbose)
            for (auto& e : out)
                LogInfo("%s candidate slot=%#lx fn@+0x%lx", name, e.first,
                        (unsigned long)e.second);
        return out;
    };
    auto ray = findMethod("Raycast", false);
    auto shape = findMethod("Shapecast", false);
    LogInfo("Raycast slots: %zu, Shapecast slots: %zu", ray.size(),
            shape.size());
    for (auto& r : ray) {
        bool sib = false;
        for (auto& s : shape) {
            if (s.first == r.first) continue;
            int64_t d = (int64_t)s.first - (int64_t)r.first;
            if (d < 0) d = -d;
            if ((uint64_t)d < 0x10000) {
                sib = true;
                break;
            }
        }
        
        Emit("RaycastBoundDesc", "WorldRoot", r.first, "unsigned __int64");
        Emit("RaycastBoundFn", "WorldRoot", r.second, "unsigned __int64");
        LogOk("WorldRoot::Raycast desc=%#lx (rva) fn@+0x%lx%s", r.first,
              (unsigned long)r.second, sib ? " +sibling" : "");
        return true;
    }
    LogBad("World FAILED: no Raycast descriptor");
    return false;
}
}  
