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
bool Inspect(uintptr_t addr) {
    char b[24];
    if (!ReadBytes(addr, b, 24)) {
        LogBad("unreadable");
        return false;
    }
    printf("bytes:");
    for (int i = 0; i < 24; ++i) printf(" %02x", (unsigned char)b[i]);
    printf("\ntext : %.24s\n", b);
    int msvc_len;
    memcpy(&msvc_len, b + 16, 4);
    printf("msvc-shape len@16=%d\n", msvc_len);
    printf("ReadString() -> %s\n", ReadString(addr).c_str());
    return true;
}
bool UgcFind() {
    struct SR {
        uintptr_t s, e;
    };
    std::vector<SR> regs;
    for (auto& r : SmallRegions()) regs.push_back({r.s, r.e});
    const char* pats[] = {"Ugc",       "Workspace", "Players",
                          "Lighting",  "Humanoid",  "Camera",
                          "Leo2834s",  "idom"};
    struct Hit {
        uintptr_t addr;
        int pi;
    };
    std::vector<Hit> hits;
    std::vector<char> buf((8 << 20) + 64);
    for (auto& r : regs) {
        size_t size = r.e - r.s, off = 0, carry = 0;
        while (off < size && hits.size() < 3000) {
            size_t n = size - off > (8 << 20) ? (8 << 20) : size - off;
            if (!ReadBytes(r.s + off, buf.data() + carry, n)) break;
            size_t len = carry + n;
            uintptr_t base = r.s + off - carry;
            for (int pi = 0; pi < 8; ++pi) {
                size_t pl = strlen(pats[pi]);
                const void* p = nullptr;
                const char* d = buf.data();
                size_t l = len;
                while ((p = memmem(d, l, pats[pi], pl))) {
                    hits.push_back(
                        {base + ((const char*)p - buf.data()), pi});
                    if (hits.size() >= 3000) break;
                    d = (const char*)p + 1;
                    l = len - (d - buf.data());
                }
            }
            size_t cn = n >= 64 ? 64 : n;
            memmove(buf.data(), buf.data() + carry + n - cn, cn);
            carry = cn;
            off += n;
        }
    }
    LogInfo("string hits: %zu", hits.size());
    
    const int KK[] = {0, 8, 16, 24, 32};
    std::vector<uint64_t> tgts;
    for (auto& h : hits)
        for (int k : KK)
            for (int j = 0; j <= 64; j += 8)
                tgts.push_back(h.addr - k - j);
    std::sort(tgts.begin(), tgts.end());
    tgts.erase(std::unique(tgts.begin(), tgts.end()), tgts.end());
    struct Slot {
        uintptr_t slot, val;
        bool operator==(const Slot& o) const {
            return slot == o.slot && val == o.val;
        }
        bool operator<(const Slot& o) const {
            return slot < o.slot || (slot == o.slot && val < o.val);
        }
    };
    std::vector<Slot> slots;
    for (auto& r : regs) {
        size_t size = r.e - r.s, off = 0, carry = 0;
        while (off < size && slots.size() < 4000) {
            size_t n = size - off > (8 << 20) ? (8 << 20) : size - off;
            if (!ReadBytes(r.s + off, buf.data() + carry, n)) break;
            size_t len = carry + n;
            uintptr_t base = r.s + off - carry;
            for (size_t i = 0; i + 8 <= len; ++i) {
                if (((base + i) & 7) != 0) continue;
                uint64_t v;
                memcpy(&v, buf.data() + i, 8);
                if (std::binary_search(tgts.begin(), tgts.end(), v))
                    slots.push_back({base + i, v});
            }
            size_t cn = n >= 64 ? 64 : n;
            memmove(buf.data(), buf.data() + carry + n - cn, cn);
            carry = cn;
            off += n;
        }
    }
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    LogInfo("holder slots: %zu", slots.size());
    
    for (auto& sl : slots) {
        char hx[129] = {};
        char cb[32];
        if (ReadBytes(sl.val, cb, 32)) {
            for (int q = 0; q < 32; ++q)
                snprintf(hx + strlen(hx), sizeof(hx), "%02x",
                         (unsigned char)cb[q]);
        }
        printf("slot=%#lx val=%#lx cont=%.64s\n    hex=%s\n", sl.slot, sl.val,
               ReadString(sl.val).c_str(), hx);
        for (int nc = 0x40; nc <= 0x90; nc += 8) {
            uintptr_t I = sl.slot - nc;
            uintptr_t holder = Read<uintptr_t>(I + 0x78);
            if (!IsValid(holder)) continue;
            uintptr_t st = Read<uintptr_t>(holder);
            uintptr_t en = Read<uintptr_t>(holder + 8);
            if (!IsValid(st) || en < st || en - st > 0x100000) continue;
            size_t nk = (en - st) / 16;
            bool hasPlace = false;
            for (uintptr_t i = 0; i < 0x1000; i += 8)
                if (Read<uint64_t>(I + i) == g_placeId) {
                    hasPlace = true;
                    break;
                }
            printf("    I=%#lx nc=%#x kids=%zu place=%d\n", I, nc, nk,
                   hasPlace);
        }
    }
    const int NCS[] = {0x58, 0x60, 0x68, 0x70, 0x78};
    int found = 0;
    for (auto& sl : slots) {
        for (int nc : NCS) {
            uintptr_t I = sl.slot - nc;
            
            uintptr_t holder = Read<uintptr_t>(I + 0x78);
            if (!IsValid(holder)) continue;
            uintptr_t st = Read<uintptr_t>(holder);
            uintptr_t en = Read<uintptr_t>(holder + 8);
            if (!IsValid(st) || en < st) continue;
            if (en - st > 0x100000) continue;
            
            bool hasPlace = false;
            uintptr_t pk = 0;
            for (uintptr_t i = 0; i < 0x1000; i += 8)
                if (Read<uint64_t>(I + i) == g_placeId) {
                    hasPlace = true;
                    pk = i;
                    break;
                }
            if (!hasPlace) continue;
            LogOk("DATAMODEL? I=%#lx slot=%#lx nc=%#x place@+%#lx", I, sl.slot, nc, (unsigned long)pk);
            auto kids = GetChildren(I);
            printf("  kids=%zu\n", kids.size());
            int shown = 0;
            for (uintptr_t c : kids) {
                if (shown++ >= 12) break;
                
                std::string best;
                char raw[97] = {};
                for (auto pr : {std::pair<int, int>{0x70, 8},
                                {0x60, 8},
                                {0x68, 8}}) {
                    uintptr_t b = Read<uintptr_t>(c + pr.first);
                    if (!IsValid(b)) continue;
                    char rb[24];
                    if (!ReadBytes(b + pr.second, rb, 24)) continue;
                    if (best.empty()) {
                        snprintf(raw, sizeof(raw),
                                 "nc=%#x n=%d hex:", pr.first, pr.second);
                        for (int q = 0; q < 24; ++q)
                            snprintf(raw + strlen(raw), sizeof(raw),
                                     " %02x", (unsigned char)rb[q]);
                        std::string ms = ReadString(b + pr.second);
                        if (ms != "invalid_str") best = ms;
                    }
                }
                printf("    kid %#lx name[%s] %s\n", c, best.c_str(), raw);
            }
            if (++found >= 4) return true;
        }
    }
    printf(found ? "FLOW PARTIAL\n" : "UGCFIND: no DataModel\n");
    return found > 0;
}


bool FindDm() {
    auto regs = SmallRegions();
    struct VHit {
        uintptr_t addr;
        int which;
    };
    std::vector<VHit> hits;
    std::vector<char> buf((8 << 20) + 64);
    uint64_t wants[3] = {g_placeId, g_gameId, g_creatorId};
    for (auto& r : regs) {
        size_t size = r.e - r.s, off = 0, carry = 0;
        while (off < size && hits.size() < 600) {
            size_t n = size - off > (8 << 20) ? (8 << 20) : size - off;
            if (!ReadBytes(r.s + off, buf.data() + carry, n)) break;
            size_t len = carry + n;
            uintptr_t base = r.s + off - carry;
            for (int w = 0; w < 3; ++w) {
                const char* d = buf.data();
                size_t l = len;
                const void* p;
                while ((p = memmem(d, l, &wants[w], 8))) {
                    hits.push_back(
                        {base + ((const char*)p - buf.data()), w});
                    if (hits.size() >= 600) break;
                    d = (const char*)p + 1;
                    l = len - (d - buf.data());
                }
            }
            size_t cn = n >= 64 ? 64 : n;
            memmove(buf.data(), buf.data() + carry + n - cn, cn);
            carry = cn;
            off += n;
        }
    }
    const char* wn[3] = {"place", "game", "creator"};
    for (auto& h : hits) printf("  valhit %s @ %#lx\n", wn[h.which], h.addr);
    
    int shown = 0;
    for (auto& h : hits) {
        if (h.which != 0) continue;
        for (uintptr_t k = 0x80; k <= 0x280; k += 8) {
            uintptr_t I = h.addr - k;
            uintptr_t holder = Read<uintptr_t>(I + 0x78);
            if (!IsValid(holder)) continue;
            uintptr_t st = Read<uintptr_t>(holder);
            uintptr_t en = Read<uintptr_t>(holder + 8);
            if (!IsValid(st) || en < st || en - st > 0x100000) continue;
            size_t nk = (en - st) / 16;
            if (nk < 5 || nk > 500) continue;
            
            bool gg = false, cc = false;
            for (uintptr_t i = 0; i < 0x400; i += 8) {
                uint64_t v = Read<uint64_t>(I + i);
                if (v == g_gameId) gg = true;
                if (v == g_creatorId) cc = true;
            }
            LogOk("CAND I=%#lx (place+%#lx) kids=%zu game=%d creator=%d", I, k, nk, gg, cc);
            auto kids = GetChildren(I);
            int sk = 0;
            for (uintptr_t c : kids) {
                if (sk++ >= 20) break;
                printf("    kid %#lx:\n", c);
                for (auto pr : {std::pair<int, int>{0x70, 8},
                                {0x60, 8},
                                {0x68, 8},
                                {0x58, 8},
                                {0x70, 0},
                                {0x70, 16}}) {
                    uintptr_t b = Read<uintptr_t>(c + pr.first);
                    if (!IsValid(b)) continue;
                    char rb[24];
                    if (!ReadBytes(b + pr.second, rb, 24)) continue;
                    char hx[64] = {};
                    for (int q = 0; q < 20; ++q)
                        snprintf(hx + strlen(hx), sizeof(hx), "%02x",
                                 (unsigned char)rb[q]);
                    char asc[21] = {};
                    for (int q = 0; q < 20; ++q)
                        asc[q] = (rb[q] >= 32 && rb[q] < 127) ? rb[q] : '.';
                    printf("      nc=%#x n=%d %s |%s|\n", pr.first, pr.second,
                           hx, asc);
                }
            }
            if (++shown >= 6) return true;
        }
    }
    printf(shown ? "FLOW PARTIAL\n" : "FINDDM: nothing\n");
    return shown > 0;
}


bool RttiHist() {
    
    if (!SnapshotData()) {
        LogBad("snapshot failed");
        return false;
    }
    std::vector<std::pair<std::string, int>> hist;
    auto bump = [&](const std::string& n) {
        for (auto& e : hist)
            if (e.first == n) {
                e.second++;
                return;
            }
        hist.emplace_back(n, 1);
    };
    size_t checked = 0;
    for (auto& s : g_snaps)
        for (size_t i = 0; i + 8 <= s.buf.size(); i += 8) {
            uint64_t obj;
            memcpy(&obj, s.buf.data() + i, 8);
            if (!IsValid(obj)) continue;
            
            uint64_t vt = L64(obj);
            if (!vt) continue;
            bool indata = false;
            for (auto& t : g_snaps)
                if (vt >= t.base && vt < t.base + t.size) {
                    indata = true;
                    break;
                }
            if (!indata) continue;
            std::string n = RttiGet(obj);
            if (n != "???") bump(n);
            if (++checked > 300000) break;
        }
    std::sort(hist.begin(), hist.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    LogInfo("checked=%zu distinct=%zu", checked, hist.size());
    for (size_t i = 0; i < hist.size() && i < 40; ++i)
        printf("   %-5d %s\n", hist[i].second, hist[i].first.c_str());
    return true;
}

bool RttiFind() {
    
    
    if (!SnapshotData()) {
        LogBad("snapshot failed");
        return false;
    }
    struct Triple {
        uintptr_t vt, ti, nm;
    };
    std::vector<Triple> triples;
    auto inSnap = [&](uint64_t a) {
        for (auto& s : g_snaps)
            if (a >= s.base && a < s.base + s.size) return true;
        return false;
    };
    for (auto& s : g_snaps)
        for (size_t i = 0; i + 8 <= s.buf.size(); i += 8) {
            uint64_t vt;
            memcpy(&vt, s.buf.data() + i, 8);
            if (!IsValid(vt) || !inSnap(vt)) continue;
            uint64_t ti = L64(vt - 16);
            if (!ti || !inSnap(ti)) continue;
            uint64_t nm = L64(ti + 8);
            if (!nm || nm < g_rxActive || nm >= g_rxActive + 0x6aa8150)
                continue;
            triples.push_back({(uintptr_t)vt, (uintptr_t)ti,
                               (uintptr_t)nm});
            if (triples.size() > 20000) break;
        }
    LogInfo("triples: %zu", triples.size());
    int shown = 0, matched = 0;
    for (auto& t : triples) {
        std::string raw = ReadRaw(t.nm, 160);
        if (raw == "invalid_str" || raw.size() < 2) continue;
        std::string dn = DemangleItanium(raw);
        if (shown < 25)
            printf("   vt=%#lx ti=%#lx %s\n", t.vt, t.ti, dn.c_str());
        ++shown;
        if (dn == "RBX::DataModel" || dn == "RBX::Graphics::VisualEngine") {
            LogOk("MATCH %-28s vt=%#lx ti=%#lx", dn.c_str(), t.vt, t.ti);
            ++matched;
        }
    }
    LogInfo("named=%d matched=%d", shown, matched);
    return true;
}

bool FindClass(int argc, char** argv) {
    
    
    uint64_t want = 0;
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--classdesc") && i + 1 < argc)
            want = strtoull(argv[++i], nullptr, 16);
    if (!want) {
        LogBad("usage: --step findclass --classdesc 0xADDR");
        return false;
    }
    LogInfo("scanning smalls for classdesc %#lx", want);
    auto regs = SmallRegions();
    std::vector<char> buf((8 << 20) + 64);
    struct Hit {
        uintptr_t slot;
    };
    std::vector<Hit> hits;
    for (auto& r : regs) {
        size_t size = r.e - r.s, off = 0, carry = 0;
        while (off < size && hits.size() < 2000) {
            size_t n = size - off > (8 << 20) ? (8 << 20) : size - off;
            if (!ReadBytes(r.s + off, buf.data() + carry, n)) {
                off += n;
                carry = 0;
                continue;
            }
            size_t len = carry + n;
            uintptr_t base = r.s + off - carry;
            for (size_t i = 0; i + 8 <= len; ++i) {
                if (((base + i) & 7) != 0) continue;
                uint64_t v;
                memcpy(&v, buf.data() + i, 8);
                if (v == want) hits.push_back({base + i});
            }
            size_t cn = n >= 64 ? 64 : n;
            memmove(buf.data(), buf.data() + carry + n - cn, cn);
            carry = cn;
            off += n;
        }
    }
    LogInfo("slots: %zu", hits.size());
    for (auto& h : hits) {
        uintptr_t I = h.slot - 0x18;
        char hdr[32];
        if (!ReadBytes(I, hdr, 32)) continue;
        uint64_t vt;
        memcpy(&vt, hdr, 8);
        float f[6];
        bool ok = true;
        for (int k = 0; k < 6; ++k) {
            uint64_t a = I + 0x180 + k * 4;
            char b[4];
            if (!ReadBytes(a, b, 4)) {
                ok = false;
                break;
            }
            memcpy(&f[k], b, 4);
        }
        if (!ok) continue;
        printf("INST? I=%#lx vt=%#lx H=%.1f Hip=%.2f JH=%.2f JP=%.1f "
               "MH=%.1f MSA=%.1f\n",
               I, vt, f[0], f[1], f[2], f[3], f[4], f[5]);
        float ws = 0, wc = 0;
        char b4[4];
        if (ReadBytes(I + 0x1C0, b4, 4)) memcpy(&ws, b4, 4);
        if (ReadBytes(I + 0x3AC, b4, 4)) memcpy(&wc, b4, 4);
        printf("   WalkSpeed=%.3f Check=%.3f\n", ws, wc);
    }
    return true;
}

bool FindDesc() {
    
    
    uint64_t setter = g_rxActive + 0x25C327A;
    uint64_t getter = g_rxActive + 0x304DCD2;
    if (!g_rxActive) {
        if (!SnapshotData()) return 1;
    }
    setter = g_rxActive + 0x25C327A;
    getter = g_rxActive + 0x304DCD2;
    LogInfo("setter=%#lx getter=%#lx", setter, getter);
    struct WR {
        uintptr_t s, e;
    };
    std::vector<WR> wrs;
    for (auto& m : ReadMaps()) {
        if (m.tag.empty() || m.tag[0] != 'r') continue;
        bool memfd = m.tag.find("/memfd") != std::string::npos;
        if (m.tag[1] == 'w' || (memfd && m.tag[2] != 'x'))
            wrs.push_back({m.s, m.e});
    }
    std::vector<char> buf((4 << 20) + 32);
    for (auto& r : wrs) {
        size_t size = r.e - r.s;
        if (size > (256ull << 20)) continue;  
        size_t off = 0, carry = 0;
        while (off < size) {
            size_t n = size - off > (4 << 20) ? (4 << 20) : size - off;
            if (!ReadBytes(r.s + off, buf.data() + carry, n)) {
                off += n;  
                carry = 0;
                continue;
            }
            size_t len = carry + n;
            uintptr_t base = r.s + off - carry;
            for (size_t i = 0; i + 8 <= len; i += 8) {
                uint64_t v;
                memcpy(&v, buf.data() + i, 8);
                if (v == setter || v == getter) {
                    uintptr_t hit = base + i;
                    printf("HIT %s @ %#lx\n",
                           v == setter ? "setter" : "getter", hit);
                    char ctx[64];
                    if (ReadBytes(hit - 32, ctx, 64)) {
                        printf("  ctx:");
                        for (int q = 0; q < 64; ++q)
                            printf(" %02x", (unsigned char)ctx[q]);
                        printf("\n");
                    }
                }
            }
            size_t cn = n >= 32 ? 32 : n;
            memmove(buf.data(), buf.data() + carry + n - cn, cn);
            carry = cn;
            off += n;
        }
    }
    return true;
}

bool DataSlide() {
    
    if (!SnapshotData()) {
        LogBad("snapshot failed");
        return false;
    }
    struct Win {
        uintptr_t addr;
        int cnt;
    };
    std::vector<Win> wins;
    
    struct Bin {
        uintptr_t base;
        int cnt;
    };
    std::vector<Bin> bins;
    for (auto& s : g_snaps) {
        for (size_t b = 0; b < s.buf.size(); b += 32768) {
            int c = 0;
            for (size_t i = b; i + 8 <= s.buf.size() && i < b + 32768;
                 i += 8) {
                uint64_t v;
                memcpy(&v, s.buf.data() + i, 8);
                if (v >= (uint64_t)g_rxActive &&
                    v < (uint64_t)g_rxActive + 0x6aa8150)
                    ++c;
            }
            if (c > 500) bins.push_back({s.base + b, c});
        }
    }
    std::sort(bins.begin(), bins.end(),
              [](auto& a, auto& b) { return a.cnt > b.cnt; });
    LogInfo("dense bins: %zu", bins.size());
    for (size_t i = 0; i < bins.size() && i < 12; ++i)
        printf("   %#lx count=%d/4096\n", bins[i].base, bins[i].cnt);
    return true;
}
}  
