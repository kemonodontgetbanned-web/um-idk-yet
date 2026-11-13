#pragma once

#include <sys/uio.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "globals.h"
#include "log.h"

#if defined(__has_include) && __has_include("int3drv.h")
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "int3drv.h"
#define HAVE_INT3 1
#endif

inline bool IsValid(uint64_t a) {
    return a > 0x10000 && a != 0xCCCCCCCCCCCCCCCCull && a < 0x800000000000ull;
}

#ifdef HAVE_INT3




inline int g_int3fd = -1;
inline pid_t g_int3pid = -1;
inline bool g_int3tried = false;
inline bool g_int3ok = false;

inline bool Int3Ensure() {
    if (g_int3tried) {
        if (g_int3ok && g_pid != g_int3pid) {
            int32_t p = g_pid;  
            if (ioctl(g_int3fd, INT3_IOC_ATTACH, &p) == 0) {
                g_int3pid = g_pid;
            } else {
                g_int3ok = false;
            }
        }
        return g_int3ok;
    }
    g_int3tried = true;
    const char* off = getenv("IX_NO_INT3");
    if (off && off[0]) {
        LogInfo("[mem] backend: process_vm_readv (IX_NO_INT3 set)");
        return false;
    }
    int fd = open(INT3DRV_DEV, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LogInfo("[mem] backend: process_vm_readv (no /dev/int3)");
        return false;
    }
    uint32_t ver = 0;
    int32_t p = g_pid;
    if (ioctl(fd, INT3_IOC_VERSION, &ver) != 0 || ver < INT3DRV_VERSION ||
        ioctl(fd, INT3_IOC_ATTACH, &p) != 0) {
        close(fd);
        LogInfo("[mem] backend: process_vm_readv (int3 attach/version failed)");
        return false;
    }
    g_int3fd = fd;
    g_int3pid = g_pid;
    g_int3ok = true;
    LogInfo("[mem] backend: int3drv v%u (/dev/int3)", ver);
    return true;
}

inline bool Int3Read(uintptr_t addr, void* out, size_t n) {
    if (n == 0 || n > INT3_MAX_SINGLE) return false;
    struct int3_rw rw{(uint64_t)addr, (uint64_t)(uintptr_t)out, (uint64_t)n};
    return ioctl(g_int3fd, INT3_IOC_READ, &rw) == 0;
}

inline bool Int3Get(uintptr_t addr, void* out, size_t n) {
    
    if (n == 8) {
        struct int3_val v{(uint64_t)addr, 0};
        if (ioctl(g_int3fd, INT3_IOC_READ_U64, &v) != 0) return false;
        memcpy(out, &v.val, 8);
        return true;
    }
    if (n == 4) {
        struct int3_val v{(uint64_t)addr, 0};
        if (ioctl(g_int3fd, INT3_IOC_READ_U32, &v) != 0) return false;
        uint32_t w = (uint32_t)v.val;
        memcpy(out, &w, 4);
        return true;
    }
    return false;
}
#endif

inline bool ProcReadBytes(uintptr_t addr, void* out, size_t n) {
    struct iovec local{out, n};
    struct iovec remote{reinterpret_cast<void*>(addr), n};
    return process_vm_readv(g_pid, &local, 1, &remote, 1, 0) ==
           (ssize_t)n;
}

inline bool ReadBytes(uintptr_t addr, void* out, size_t n) {
    if (!IsValid(addr)) return false;
#ifdef HAVE_INT3
    if (Int3Ensure()) {
        if (Int3Read(addr, out, n)) return true;
        
        return false;
    }
#endif
    return ProcReadBytes(addr, out, n);
}

template <typename T>
T Read(uintptr_t addr) {
    T v{};
    if (!IsValid(addr)) return v;
#ifdef HAVE_INT3
    if ((sizeof(T) == 8 || sizeof(T) == 4) && Int3Ensure() &&
        Int3Get(addr, &v, sizeof(v)))
        return v;
#endif
    ReadBytes(addr, &v, sizeof(v));
    return v;
}


inline std::string ReadString(uintptr_t addr) {
    if (!IsValid(addr)) return "invalid_str";
    int len = Read<int>(addr + 0x10);
    if (len > 15000 || len <= 0) return "invalid_str";
    uintptr_t s = (len >= 16) ? Read<uintptr_t>(addr) : addr;
    if (!IsValid(s)) return "invalid_str";
    std::string out;
    for (int i = 0; i < 200 && i < len + 4; ++i) {
        char c = Read<char>(s + i);
        if (c == 0) break;
        out.push_back(c);
    }
    return out.empty() ? "invalid_str" : out;
}

inline std::string ReadRaw(uintptr_t addr, size_t cap = 256) {
    if (!IsValid(addr)) return "invalid_str";
    std::string out;
    for (size_t i = 0; i < cap; ++i) {
        char c = Read<char>(addr + i);
        if (c == 0) break;
        out.push_back(c);
    }
    return out;
}


struct MapRange {
    uintptr_t s, e;
    std::string tag;
};



inline bool TextFileRange(uintptr_t& off, uintptr_t& size) {
    static bool done = false;
    static uintptr_t o = 0, sz = 0;
    if (!done) {
        done = true;
        FILE* f = fopen(g_dumpedPath.c_str(), "rb");
        if (f) {
            unsigned char eh[64];
            if (fread(eh, 1, 64, f) == 64 && eh[0] == 0x7f && eh[1] == 'E') {
                uint64_t phoff;
                uint16_t phnum, phentsz;
                memcpy(&phoff, eh + 32, 8);
                memcpy(&phentsz, eh + 54, 2);
                memcpy(&phnum, eh + 56, 2);
                for (int i = 0; i < phnum && i < 32; ++i) {
                    unsigned char ph[56];
                    fseek(f, (long)(phoff + (uint64_t)i * phentsz), SEEK_SET);
                    if (fread(ph, 1, sizeof(ph), f) != sizeof(ph)) break;
                    uint32_t type, flags;
                    uint64_t poff, filesz;
                    memcpy(&type, ph, 4);
                    memcpy(&flags, ph + 4, 4);
                    memcpy(&poff, ph + 8, 8);
                    memcpy(&filesz, ph + 32, 8);
                    if (type == 1 && (flags & 5) == 5) {
                        o = (uintptr_t)poff;
                        sz = (uintptr_t)filesz;
                        break;
                    }
                }
            }
            fclose(f);
        }
    }
    off = o;
    size = sz;
    return sz != 0;
}

inline std::vector<MapRange> ProcReadMaps() {
    std::vector<MapRange> out;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", g_pid);
    FILE* f = fopen(path, "r");
    if (!f) return out;
    char* line = nullptr;
    size_t cap = 0;
    while (getline(&line, &cap, f) != -1) {
        unsigned long s, e;
        char p[8] = {};
        if (sscanf(line, "%lx-%lx %7s", &s, &e, p) != 3) continue;
        const char* sl = strchr(line, '/');
        out.push_back({s, e, std::string(1, p[0]) + p[1] + p[2] +
                                   (sl ? sl : "")});
    }
    fclose(f);
    free(line);
    return out;
}

inline std::vector<MapRange> ReadMaps() {
#ifdef HAVE_INT3
    if (Int3Ensure()) {
        
        
        std::vector<MapRange> out;
        struct int3_map m{};
        char path[512];
        m.path_ptr = (uint64_t)(uintptr_t)path;
        m.path_cap = sizeof(path);
        m.cursor = 0;
        uintptr_t lastEnd = 0;
        bool fail = false;
        while (ioctl(g_int3fd, INT3_IOC_MAPS_NEXT, &m) == 0) {
            if (m.vm_end <= lastEnd) {
                fail = true;
                break;
            }
            lastEnd = m.vm_end;
            char p[4] = {(m.vm_flags & INT3_VM_READ) ? 'r' : '-',
                         (m.vm_flags & INT3_VM_WRITE) ? 'w' : '-',
                         (m.vm_flags & INT3_VM_EXEC) ? 'x' : '-'};
            out.push_back({(uintptr_t)m.vm_start, (uintptr_t)m.vm_end,
                           std::string(1, p[0]) + p[1] + p[2] +
                               (path[0] == '/' ? path : "")});
            m.cursor = m.vm_end;
            m.path_ptr = (uint64_t)(uintptr_t)path;
            m.path_cap = sizeof(path);
        }
        if (!fail && !out.empty()) return out;
    }
#endif
    return ProcReadMaps();
}
struct SReg {
    uintptr_t s, e;
};

inline std::vector<SReg> SmallRegions() {
    
    std::vector<SReg> regs;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/smaps", g_pid);
    FILE* f = fopen(path, "r");
    if (!f) return regs;
    char* line = nullptr;
    size_t cap = 0;
    uintptr_t st = 0, en = 0;
    char perms[8] = {};
    long rss = 0;
    bool have = false;
    auto flush = [&]() {
        if (!have) return;
        if (rss > 0 && perms[0] == 'r' && perms[1] == 'w' &&
            en - st <= (256ull << 20))
            regs.push_back({st, en});
    };
    while (getline(&line, &cap, f) != -1) {
        unsigned long s, e;
        char p[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, p) == 3 && p[2] != 0) {
            flush();
            st = s;
            en = e;
            memcpy(perms, p, sizeof(perms));
            rss = 0;
            have = true;
        } else if (sscanf(line, "Rss: %ld", &rss) == 1) {
        }
    }
    flush();
    fclose(f);
    free(line);
    return regs;
}
