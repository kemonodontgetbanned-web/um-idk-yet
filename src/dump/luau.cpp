#include "dump.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "globals.h"
#include "log.h"
#include "mem.h"
#include "scanner.h"















namespace Dump {
namespace {

static std::vector<char> g_file;
static uintptr_t g_t0 = 0, g_tsz = 0;  

static bool LoadFile() {
    FILE* f = fopen(g_dumpedPath.c_str(), "rb");
    if (!f) {
        LogBad("Luau FAILED: no reference binary");
        return false;
    }
    char b[1 << 20];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) g_file.insert(g_file.end(), b, b + n);
    fclose(f);
    if (!TextFileRange(g_t0, g_tsz) || g_tsz == 0) {
        LogBad("Luau FAILED: no .text range");
        return false;
    }
    return true;
}

static inline unsigned char U8(size_t i) { return static_cast<unsigned char>(g_file[i]); }
static inline bool InText(uint64_t va) { return va >= g_t0 && va < g_t0 + g_tsz; }
static inline bool Have(size_t i, size_t n) { return i + n <= g_file.size(); }


static std::vector<uint64_t> FindStrings(const char* name) {
    std::vector<uint64_t> out;
    size_t nl = strlen(name);
    size_t off = 0;
    while (off + nl + 1 <= g_file.size()) {
        const void* p = memmem(g_file.data() + off, g_file.size() - off, name, nl);
        if (!p) break;
        size_t s = (const char*)p - g_file.data();
        off = s + 1;
        if (g_file[s + nl] != 0) continue;
        if (s > 0) {
            unsigned char pre = g_file[s - 1];
            if ((pre >= 'A' && pre <= 'Z') || (pre >= 'a' && pre <= 'z') ||
                (pre >= '0' && pre <= '9') || pre == '_')
                continue;
        }
        out.push_back(s);
        if (out.size() >= 8) break;
    }
    return out;
}


static bool RipDecode(size_t pos, uint64_t& target, bool& isStore) {
    size_t lim = g_file.size();
    if (pos + 7 <= lim && U8(pos) >= 0x48 && U8(pos) <= 0x4F &&
        (U8(pos + 1) == 0x8D || U8(pos + 1) == 0x8B || U8(pos + 1) == 0x89) &&
        ((U8(pos + 2) & 0xC7) == 0x05)) {
        int32_t disp;
        memcpy(&disp, &g_file[pos + 3], 4);
        target = pos + 7 + (int64_t)disp;
        isStore = (U8(pos + 1) == 0x89);
        return true;
    }
    if (pos + 6 <= lim && (U8(pos) == 0x8D || U8(pos) == 0x8B || U8(pos) == 0x89) &&
        ((U8(pos + 1) & 0xC7) == 0x05)) {
        int32_t disp;
        memcpy(&disp, &g_file[pos + 2], 4);
        target = pos + 6 + (int64_t)disp;
        isStore = (U8(pos) == 0x89);
        return true;
    }
    return false;
}


static std::vector<size_t> StringRefs(uint64_t strVA) {
    std::vector<size_t> out;
    for (size_t i = 0; i + 7 < g_file.size(); ++i) {
        uint64_t t = 0;
        bool st = false;
        if (!RipDecode(i, t, st) || st) continue;
        if (t == strVA) {
            if (InText(i)) out.push_back(i);
            i += 6;
        }
    }
    return out;
}



static uint64_t FuncEntry(size_t pos) {
    size_t lo = pos > 2048 ? pos - 2048 : 0;
    for (size_t k = pos; k-- > lo;) {
        if (U8(k) == 0x55 && Have(k + 3, 1) && U8(k + 1) == 0x48 && U8(k + 2) == 0x89 &&
            U8(k + 3) == 0xE5)
            return k;
        if (k == 0) break;
    }
    return 0;
}

static uint64_t CallTarget(size_t pos) {
    if (!Have(pos, 5) || U8(pos) != 0xE8) return 0;
    int32_t rel;
    memcpy(&rel, &g_file[pos + 1], 4);
    uint64_t t = pos + 5 + (int64_t)rel;
    return InText(t) ? t : 0;
}

static std::vector<uint64_t> CallsIn(uint64_t start, size_t len) {
    std::vector<uint64_t> out;
    for (size_t i = start; i + 5 < start + len && i + 5 < g_file.size(); ++i) {
        uint64_t t = CallTarget(i);
        if (t) {
            out.push_back(t);
            i += 4;
        }
    }
    return out;
}


static inline size_t SkipRex(size_t p) {
    if (p < g_file.size() && U8(p) >= 0x40 && U8(p) <= 0x4F) return p + 1;
    return p;
}


struct ResumeInfo {
    uint64_t entry = 0, errFn = 0, growFn = 0, newStrFn = 0;
    int status = -1, isActive = -1, memcat = -1;
    int64_t ciA = -1, ciB = -1;  
    int ncCalls = -1, baseCCalls = -1;
    int64_t globalSlot = -1, grayHead = -1, gclist = -1;
    int top = -1, stackLast = -1, stackSize = -1;
    int mMemcat = -1, mTt = -1, mMarked = -1;  
    int gGlobal = -1, gWhite = -1;
    int64_t strt = -1;
    int stackBase = -1, openUpval = -1, baseField = -1;
    int64_t ciLo = -1, ciHi = -1;  
    int tT = -1, tV = -1, tE = -1;  
};

static bool ParseResumeError(ResumeInfo& ri);
static bool ParseNewStr(ResumeInfo& ri);
static bool ParseRealloc(ResumeInfo& ri);

static bool LuauResume(ResumeInfo& ri) {
    auto strs = FindStrings("cannot resume non-suspended coroutine");
    if (strs.empty()) {
        LogBad("MISS   Luau resume_start (no error string)");
        return false;
    }
    auto refs = StringRefs(strs[0]);
    uint64_t entry = 0;
    for (size_t r : refs) {
        entry = FuncEntry(r);
        if (entry && InText(entry)) break;
        entry = 0;
    }
    if (!entry) {
        LogBad("MISS   Luau resume_start (no entry)");
        return false;
    }
    ri.entry = entry;
    LogInfo("resume_start=%#lx", (unsigned long)entry);
    size_t win = (size_t)entry + 512;
    if (win > g_file.size()) win = g_file.size();
    
    for (size_t i = entry; i + 5 < win; ++i) {
        size_t q = i + ((U8(i) == 0x48) ? 1 : 0);
        if (q + 4 >= g_file.size()) break;
        if (U8(q) == 0x0F && U8(q + 1) == 0xB6 && U8(q + 2) == 0x47) {
            ri.status = (int8_t)U8(q + 3);
            break;
        }
    }
    
    for (size_t i = entry; i + 32 < win; ++i) {
        if (U8(i) != 0x48 || U8(i + 1) != 0x8B) continue;
        unsigned char mr = U8(i + 2);
        if (mr != 0x47 && mr != 0x87) continue;
        int64_t a = (mr == 0x47) ? (int8_t)U8(i + 3) : 0;
        size_t adv = 0;
        if (mr == 0x47) {
            adv = 4;
        } else {
            int32_t a32 = 0;
            memcpy(&a32, &g_file[i + 3], 4);
            a = a32;
            adv = 7;
        }
        for (size_t k = i + adv; k + 7 < win && k < i + 24; ++k) {
            if (U8(k) != 0x48 || U8(k + 1) != 0x3B) continue;
            unsigned char m2 = U8(k + 2);
            if (m2 != 0x47 && m2 != 0x87) continue;
            int64_t b = (m2 == 0x47) ? (int8_t)U8(k + 3) : 0;
            if (m2 == 0x87) {
                int32_t b32 = 0;
                memcpy(&b32, &g_file[k + 3], 4);
                b = b32;
            }
            ri.ciA = a;
            ri.ciB = b;
            break;
        }
        if (ri.ciA >= 0) break;
    }
    
    for (size_t i = entry; i + 24 < win; ++i) {
        size_t q = i + ((U8(i) == 0x48) ? 1 : 0);
        if (q + 4 >= g_file.size()) break;
        if (U8(q) != 0x0F || U8(q + 1) != 0xB7 || U8(q + 2) != 0x46) continue;
        
        
        int disps[8], nd = 0;
        for (size_t k = i; k + 4 < win && k < i + 64 && nd < 8; ++k) {
            if (U8(k) == 0x66 && U8(k + 1) == 0x89 && U8(k + 2) == 0x47)
                disps[nd++] = (int8_t)U8(k + 3);
        }
        if (nd >= 1) ri.ncCalls = disps[0];
        for (int j = 1; j < nd; ++j) {
            if (disps[j] == disps[0] + 2) {
                ri.baseCCalls = disps[j];
                break;
            }
        }
        if (nd > 1 && ri.baseCCalls < 0)
            LogBad("nccalls pair not consecutive (first 0x%x)", disps[0]);
        break;
    }
    
    for (size_t i = entry; i + 4 < win; ++i) {
        if (U8(i) == 0xC6 && U8(i + 1) == 0x47 && U8(i + 3) == 0x01) {
            ri.isActive = (int8_t)U8(i + 2);
            break;
        }
    }
    
    
    
    for (size_t i = entry; i + 16 < win; ++i) {
        if (U8(i) != 0x8A || U8(i + 1) != 0x4F) continue;
        int cand = (int8_t)U8(i + 2);
        for (size_t k = i; k + 8 < win && k < i + 12; ++k) {
            if (U8(k) == 0xF6 && U8(k + 1) == 0xC1 && U8(k + 2) == 0x04) {
                ri.memcat = cand;
                break;
            }
        }
        if (ri.memcat >= 0) break;
    }
    
    
    for (size_t i = entry; i + 48 < win; ++i) {
        if (U8(i) != 0x48 || U8(i + 1) != 0x8B || U8(i + 2) != 0x57) continue;
        int a = (int8_t)U8(i + 3);
        for (size_t k = i + 4; k + 16 < win && k < i + 20; ++k) {
            if (U8(k) != 0x48 || U8(k + 1) != 0x8B || U8(k + 2) != 0x4A) continue;
            int b = (int8_t)U8(k + 3);
            for (size_t m = k + 4; m + 8 < win && m < k + 16; ++m) {
                if (U8(m) != 0x48 || U8(m + 1) != 0x89 || U8(m + 2) != 0x4F) continue;
                int c = (int8_t)U8(m + 3);
                for (size_t n = m + 4; n + 4 < win && n < m + 12; ++n) {
                    if (U8(n) == 0x48 && U8(n + 1) == 0x89 && U8(n + 2) == 0x7A &&
                        (int8_t)U8(n + 3) == b) {
                        ri.globalSlot = a;
                        ri.grayHead = b;
                        ri.gclist = c;
                        break;
                    }
                }
                break;
            }
            break;
        }
        if (ri.globalSlot >= 0) break;
    }
    for (size_t r : refs) {
        for (size_t i = r; i + 5 < win; ++i) {
            uint64_t t = CallTarget(i);
            if (t) {
                ri.errFn = t;
                break;
            }
        }
        if (ri.errFn) break;
    }
    if (ri.status < 0) LogBad("MISS   lua_State.Status (resume_start)");
    if (ri.ciA < 0) LogBad("MISS   lua_State ci pair (resume_start)");
    if (ri.ncCalls < 0) LogBad("MISS   lua_State.NCcalls (resume_start)");
    if (ri.isActive < 0) LogBad("MISS   lua_State.IsActive (resume_start)");
    if (ri.memcat < 0) LogBad("MISS   lua_State.Memcat (resume_start)");
    if (ri.globalSlot < 0) LogBad("MISS   lua_State barrier triple (resume_start)");
    if (!ri.errFn) LogBad("MISS   Luau resume_error (resume_start)");
    return ri.status >= 0 && ri.errFn != 0;
}

static bool ParseResumeError(ResumeInfo& ri) {
    if (!ri.errFn) return false;
    size_t win = (size_t)ri.errFn + 512;
    if (win > g_file.size()) win = g_file.size();
    
    
    int64_t topCand = -1;
    {
        int64_t cands[8];
        int nc = 0;
        for (size_t i = ri.errFn; i + 4 < win && nc < 8; ++i) {
            uint8_t rx = U8(i);
            if ((rx == 0x4C || rx == 0x4D) && U8(i + 1) == 0x8B &&
                (U8(i + 2) == 0x7F || U8(i + 2) == 0x7E))
                cands[nc++] = (int8_t)U8(i + 3);
        }
        for (int c = 0; c < nc && topCand < 0; ++c) {
            bool tt6 = false, storeBack = false;
            for (size_t i = ri.errFn; i + 7 < win; ++i) {
                size_t j = i + ((U8(i) == 0x41) ? 1 : 0);
                if (j + 6 >= g_file.size()) break;
                if (U8(j) == 0xC7 && U8(j + 1) == 0x45 && U8(j + 2) == 0x0C &&
                    U8(j + 3) == 0x06)
                    tt6 = true;
                size_t q = SkipRex(i);
                if (q + 3 < g_file.size() && U8(q) == 0x89 && ((U8(q + 1) >> 6) & 3) == 1 &&
                    (U8(q + 1) & 7) == 7 && (int8_t)U8(q + 2) == (int8_t)cands[c])
                    storeBack = true;
                if (tt6 && storeBack) break;
            }
            if (tt6 && storeBack) topCand = cands[c];
        }
    }
    bool tt6got = topCand >= 0, backGot = topCand >= 0;
    if (topCand >= 0 && tt6got && backGot) {
        ri.top = (int)topCand;
        ri.tT = 0x0C;
        LogInfo("Top=%#lx (resume_error + tt6 x-check)", (unsigned long)ri.top);
    } else {
        LogBad("MISS   lua_State.Top (resume_error top=%lld tt6=%d back=%d)", (long long)topCand,
               (int)tt6got, (int)backGot);
    }
    
    for (size_t i = ri.errFn; i + 16 < win; ++i) {
        if (U8(i) != 0x48 || U8(i + 1) != 0x8B || U8(i + 2) != 0x4B) continue;
        int lim = (int8_t)U8(i + 3);
        for (size_t k = i + 4; k + 8 < win && k < i + 24; ++k) {
            if (U8(k) == 0x48 && U8(k + 1) == 0x83 && U8(k + 2) == 0xF9 && U8(k + 3) == 0x10) {
                ri.stackLast = lim;
                break;
            }
        }
        if (ri.stackLast >= 0) break;
    }
    if (ri.stackLast < 0) LogBad("MISS   lua_State.StackLast (resume_error)");
    
    
    for (size_t i = ri.errFn; i + 9 < win; ++i) {
        if (U8(i) != 0x8B || U8(i + 1) != 0x43) continue;
        if (U8(i + 3) != 0x8D || U8(i + 4) != 0x0C || U8(i + 5) != 0x00) continue;
        if (U8(i + 6) != 0x8D || U8(i + 7) != 0x70 || U8(i + 8) != 0x01) continue;
        ri.stackSize = (int8_t)U8(i + 2);
        break;
    }
    if (ri.stackSize < 0) LogBad("MISS   lua_State.Stacksize (resume_error)");
    auto calls = CallsIn(ri.errFn, 512);
    if (calls.size() >= 2) {
        ri.newStrFn = calls[1];
        LogInfo("newlstr=%#lx (2nd call in resume_error)", (unsigned long)ri.newStrFn);
    } else {
        LogBad("MISS   Luau newlstr (resume_error calls)");
    }
    for (size_t ci = 0; ci < calls.size(); ++ci) {
        for (size_t i = ri.errFn; i + 5 < win; ++i) {
            if (CallTarget(i) != calls[ci]) continue;
            if (i >= 2 && U8(i - 2) == 0x31 && U8(i - 1) == 0xD2) {  
                ri.growFn = calls[ci];
                LogInfo("reallocstack=%#lx (xor-edx call)", (unsigned long)ri.growFn);
            }
            break;
        }
        if (ri.growFn) break;
    }
    if (!ri.growFn) LogBad("MISS   Luau reallocstack (resume_error calls)");
    return ri.top >= 0;
}

static bool ParseNewStr(ResumeInfo& ri) {
    if (!ri.newStrFn) return false;
    size_t win = (size_t)ri.newStrFn + 640;
    if (win > g_file.size()) win = g_file.size();
    
    for (size_t i = ri.newStrFn; i + 4 < win; ++i) {
        if (U8(i) == 0x48 && U8(i + 1) == 0x8B && U8(i + 2) == 0x43) {
            ri.gGlobal = (int8_t)U8(i + 3);
            break;
        }
    }
    if (ri.gGlobal < 0) {
        LogBad("MISS   lua_State.GlobalState (newlstr)");
        return false;
    }
    LogInfo("GlobalState=0x%x (newlstr global load)", ri.gGlobal);
    
    
    
    int64_t buckets = -1, sizeD = -1;
    {
        struct Hit {
            size_t pos;
            int disp;
        };
        std::vector<Hit> bs, ss;
        for (size_t i = ri.newStrFn; i + 7 < win; ++i) {
            if (U8(i) == 0x48 && U8(i + 1) == 0x8B && U8(i + 2) == 0x70)
                bs.push_back({i, (int8_t)U8(i + 3)});
            if (U8(i) == 0x49 && U8(i + 1) == 0x8B && U8(i + 2) == 0x74 && U8(i + 3) == 0x24)
                bs.push_back({i, (int8_t)U8(i + 4)});
            if (U8(i) == 0x48 && U8(i + 1) == 0x63 && U8(i + 2) == 0x48)
                ss.push_back({i, (int8_t)U8(i + 3)});
            if (U8(i) == 0x49 && U8(i + 1) == 0x63 && U8(i + 2) == 0x4C && U8(i + 3) == 0x24)
                ss.push_back({i, (int8_t)U8(i + 4)});
        }
        for (auto& b : bs)
            for (auto& s : ss) {
                size_t d = b.pos > s.pos ? b.pos - s.pos : s.pos - b.pos;
                if (d < 128 && b.disp == s.disp + 8) {
                    buckets = b.disp;
                    sizeD = s.disp;
                    break;
                }
            }
    }
    
    
    int64_t nuseD = -1;
    for (size_t i = ri.newStrFn; i + 8 < win; ++i) {
        if (U8(i) != 0x8B || U8(i + 1) != 0x48) continue;
        int cand = (int8_t)U8(i + 2);
        bool inc = false, back = false;
        for (size_t k = i; k + 12 < win && k < i + 24; ++k) {
            if (U8(k) == 0xFF && U8(k + 1) == 0xC1) inc = true;
            if (U8(k) == 0x89 && U8(k + 1) == 0x48 && (int8_t)U8(k + 2) == cand && inc)
                back = true;
        }
        if (inc && back) {
            nuseD = cand;
            break;
        }
    }
    if (buckets >= 0 && sizeD >= 0 && nuseD == sizeD + 4) {
        ri.strt = sizeD;
        LogInfo("Strt=%#llx (buckets+0x%llx size+0x%llx nuse+0x%llx)", (unsigned long long)ri.strt,
                (unsigned long long)buckets, (unsigned long long)sizeD,
                (unsigned long long)(sizeD + 4));
    } else {
        LogBad("MISS   global_State.Strt (buckets=%lld size=%lld nuse=%lld)", (long long)buckets,
               (long long)sizeD, (long long)nuseD);
    }
    
    for (size_t i = ri.newStrFn; i + 5 < win; ++i) {
        if (U8(i) == 0x8A && U8(i + 1) == 0x40 && U8(i + 3) == 0x24 && U8(i + 4) == 0x03) {
            ri.gWhite = (int8_t)U8(i + 2);
            break;
        }
    }
    if (ri.gWhite < 0) LogBad("MISS   global_State.Currentwhite (newlstr)");
    
    
    
    bool st2 = false, st1 = false, st0 = false, src4 = false;
    for (size_t i = ri.newStrFn; i + 6 < win; ++i) {
        if (U8(i) == 0x8A && U8(i + 1) == 0x43 && U8(i + 2) == 0x04) src4 = true;
        if (U8(i) == 0x41 && U8(i + 1) == 0x88 && U8(i + 2) == 0x46 && U8(i + 3) == 0x02)
            st2 = true;
        if (U8(i) == 0x41 && U8(i + 1) == 0xC6 && U8(i + 2) == 0x46 && U8(i + 3) == 0x01 &&
            U8(i + 4) == 0x06)
            st1 = true;
        if (U8(i) == 0x41 && U8(i + 1) == 0x88 &&
            ((U8(i + 2) == 0x46 && U8(i + 3) == 0x00) || U8(i + 2) == 0x06) && src4)
            st0 = true;
    }
    if (st2 && st1 && st0) {
        ri.mMemcat = 0;
        ri.mTt = 1;
        ri.mMarked = 2;
        LogInfo("CommonHeader={memcat@0,tt@1,marked@2} (init triple)");
    } else {
        LogBad("MISS   CommonHeader order (triple %d%d%d)", (int)st0, (int)st1, (int)st2);
    }
    
    
    
    bool tlen = false, tdata = false, tnext = false, thash = false;
    for (size_t i = ri.newStrFn; i + 5 < win; ++i) {
        if ((U8(i) == 0x41 && U8(i + 1) == 0x8B && U8(i + 2) == 0x46 && U8(i + 3) == 0x14) ||
            (U8(i) == 0x45 && U8(i + 1) == 0x89 && U8(i + 2) == 0x7E && U8(i + 3) == 0x14))
            tlen = true;
        if (U8(i) == 0x4C && U8(i + 1) == 0x8D && U8(i + 2) == 0x7E && U8(i + 3) == 0x18)
            tdata = true;
        uint8_t drx = U8(i);
        if ((drx == 0x4C || drx == 0x4D) && U8(i + 1) == 0x8D && ((U8(i + 2) & 0xC7) == 0x46) &&
            U8(i + 3) == 0x18)
            tdata = true;
        if (U8(i) == 0x49 && U8(i + 1) == 0x89 && U8(i + 2) == 0x7E && U8(i + 3) == 0x08)
            tnext = true;
        if (U8(i) == 0x45 && U8(i + 1) == 0x89 && U8(i + 2) == 0x66 && U8(i + 3) == 0x10)
            thash = true;
    }
    if (!(tlen && tdata && tnext && thash))
        LogInfo("TString shape partial (len=%d data=%d next=%d hash=%d)", (int)tlen, (int)tdata,
                (int)tnext, (int)thash);
    ri.tV = 0x00;
    ri.tE = 0x08;
    return ri.strt >= 0 && ri.mTt >= 0;
}

static bool ParseRealloc(ResumeInfo& ri) {
    if (!ri.growFn) return false;
    size_t win = (size_t)ri.growFn + 1024;
    if (win > g_file.size()) win = g_file.size();
    
    
    bool gate = false;
    for (size_t i = ri.growFn; i + 6 < win; ++i) {
        if (U8(i) != 0x81 || U8(i + 1) != 0xFE) continue;
        uint32_t im = 0;
        memcpy(&im, &g_file[i + 2], 4);
        if (im == 0x4000001 || im == 0x04000001) {
            gate = true;
            LogInfo("reallocstack gate imm=0x%x", im);
            break;
        }
    }
    if (!gate) {
        LogBad("MISS   reallocstack gate (growFn=%#lx suspect)", (unsigned long)ri.growFn);
        return false;
    }
    
    for (size_t i = ri.growFn; i + 4 < win; ++i) {
        if (U8(i) != 0x4C || U8(i + 1) != 0x8B || U8(i + 2) != 0x73) continue;
        int st = (int8_t)U8(i + 3);
        for (size_t k = i; k + 4 < win && k < i + 256; ++k) {
            if (U8(k) == 0x48 && U8(k + 1) == 0x89 && U8(k + 2) == 0x43 &&
                (int8_t)U8(k + 3) == st) {
                ri.stackBase = st;
                break;
            }
        }
        if (ri.stackBase >= 0) break;
    }
    if (ri.stackBase < 0) LogBad("MISS   lua_State.Stack (reallocstack)");
    
    for (size_t i = ri.growFn; i + 7 < win; ++i) {
        if (U8(i) != 0x48 || U8(i + 1) != 0x8B || U8(i + 2) != 0x48 || U8(i + 3) != 0xD0)
            continue;
        for (size_t k = i + 4; k + 4 < win && k < i + 24; ++k) {
            if (U8(k) == 0x48 && U8(k + 1) == 0x89 && U8(k + 2) == 0x4B) {
                ri.baseField = (int8_t)U8(k + 3);
                break;
            }
        }
        if (ri.baseField >= 0) break;
    }
    if (ri.baseField < 0) LogBad("MISS   lua_State.Base (reallocstack fornewci)");
    
    
    
    
    for (size_t i = ri.growFn; i + 4 < win; ++i) {
        if (U8(i) != 0x48 || U8(i + 1) != 0x8B || U8(i + 2) != 0x4B) continue;
        int ou = (int8_t)U8(i + 3);
        bool r8 = false, r20 = false;
        for (size_t k = i; k + 4 < win && k < i + 128; ++k) {
            size_t q = SkipRex(k);
            if (q + 3 >= g_file.size()) continue;
            if (U8(q) == 0x8B && U8(q + 1) == 0x51 && U8(q + 2) == 0x08) r8 = true;
            if (U8(q) == 0x8B && U8(q + 1) == 0x71 && U8(q + 2) == 0x20) r20 = true;
        }
        if (r8 && r20) {
            ri.openUpval = ou;
            break;
        }
    }
    if (ri.openUpval < 0) LogBad("MISS   lua_State.OpenUpval (reallocstack)");
    
    
    {
        bool stride = false;
        for (size_t i = ri.growFn; i + 4 < win; ++i) {
            uint8_t rx = U8(i);
            if (rx < 0x48 || rx > 0x4F) continue;
            if (U8(i + 1) != 0x83) continue;
            unsigned char mr = U8(i + 2);
            if (((mr >> 6) & 3) != 3 || ((mr >> 3) & 7) != 0) continue;
            if (U8(i + 3) != 0x30) continue;
            stride = true;
            break;
        }
        if (stride)
            LogInfo("CallInfo stride 0x30 confirmed (realloc ci-loop)");
        else
            LogBad("MISS   CallInfo stride (no add-30h in reallocstack)");
    }
    return ri.stackBase >= 0;
}

}  


namespace {

struct PrecallInfo {
    uint64_t entry = 0, resumeFn = 0;
    int ci = -1, endCi = -1, stackLast = -1;
    int fBase = -1, fFunc = -1, fTop = -1, fP = -1, fSavedpc = -1, fNr = -1, fFlags = -1;
    int clIsC = -1, clSize = -1, clFunc = -1, clProto = -1, clCont = -1;
};

static bool LuauPrecall(PrecallInfo& pi, int topSlot) {
    auto strs = FindStrings("cannot resume dead coroutine");
    if (strs.empty()) {
        LogBad("MISS   Luau resume (no dead-coroutine string)");
        return false;
    }
    auto refs = StringRefs(strs[0]);
    uint64_t resumeFn = 0;
    for (size_t r : refs) {
        resumeFn = FuncEntry(r);
        if (resumeFn && InText(resumeFn)) break;
        resumeFn = 0;
    }
    if (!resumeFn) {
        LogBad("MISS   Luau resume (no entry)");
        return false;
    }
    pi.resumeFn = resumeFn;
    LogInfo("resume=%#lx", (unsigned long)resumeFn);
    size_t win = (size_t)resumeFn + 1024;
    if (win > g_file.size()) win = g_file.size();
    
    for (size_t i = resumeFn; i + 5 < win; ++i) {
        uint64_t t = CallTarget(i);
        if (!t) continue;
        for (size_t k = t; k + 4 < t + 64 && k + 4 < g_file.size(); ++k) {
            if (U8(k) == 0x83 && U8(k + 1) == 0x7E && U8(k + 2) == 0x0C && U8(k + 3) == 0x08) {
                pi.entry = t;
                break;
            }
        }
        if (pi.entry) break;
    }
    if (!pi.entry) {
        LogBad("MISS   Luau precall (no tag-8 call in resume)");
        return false;
    }
    LogInfo("precall=%#lx", (unsigned long)pi.entry);
    size_t pw = (size_t)pi.entry + 1024;
    if (pw > g_file.size()) pw = g_file.size();
    
    
    for (size_t i = pi.entry; i + 16 < pw; ++i) {
        if (U8(i) != 0x4C || U8(i + 1) != 0x8B || U8(i + 2) != 0x7B) continue;
        int ci = (int8_t)U8(i + 3);
        for (size_t k = i + 4; k + 12 < pw && k < i + 24; ++k) {
            if (U8(k) != 0x4C || U8(k + 1) != 0x3B || U8(k + 2) != 0x7B) continue;
            int ec = (int8_t)U8(k + 3);
            for (size_t m = k + 4; m + 12 < pw && m < k + 32; ++m) {
                if (U8(m) != 0x49 || U8(m + 1) != 0x83 || U8(m + 2) != 0xC7 ||
                    U8(m + 3) != 0x30)
                    continue;
                for (size_t n = m + 4; n + 4 < pw && n < m + 16; ++n) {
                    if (U8(n) == 0x4C && U8(n + 1) == 0x89 && U8(n + 2) == 0x7B &&
                        (int8_t)U8(n + 3) == ci) {
                        pi.ci = ci;
                        pi.endCi = ec;
                        break;
                    }
                }
                break;
            }
            if (pi.ci >= 0) break;
        }
        if (pi.ci >= 0) break;
    }
    if (pi.ci < 0) LogBad("MISS   CallInfo ci/endCi (precall incr)");
    
    
    
    
    
    if (topSlot >= 0) {
        struct Load {
            size_t pos;
            int dst, disp;
        };
        std::vector<Load> loads;
        for (size_t i = pi.entry; i + 8 < pw; ++i) {
            size_t q = SkipRex(i);
            if (q + 3 >= g_file.size()) continue;
            uint8_t op = U8(q);
            if (op != 0x8B && op != 0x3B && op != 0x2B) continue;
            unsigned char mr = U8(q + 1);
            int mod = (mr >> 6) & 3;
            if (mod == 0 || (mr & 7) != 3) continue;  
            int d = (mod == 1) ? (int8_t)U8(q + 2) : 0;
            if (mod == 2) memcpy(&d, &g_file[q + 2], 4);
            int dst = ((mr >> 3) & 7) + ((q != i && (U8(i) & 4)) ? 8 : 0);
            if (op == 0x8B)
                loads.push_back({i, dst, d});
            else if (d == topSlot) {
                for (auto& l : loads) {
                    if (l.dst == dst && l.disp != topSlot && l.disp >= 0 &&
                        l.disp < 0x1000 &&
                        (l.pos > i ? l.pos - i : i - l.pos) < 48) {
                        pi.stackLast = l.disp;
                        break;
                    }
                }
                if (pi.stackLast < 0) {
                    
                    for (size_t k = i + 3; k + 4 < pw && k < i + 48; ++k) {
                        size_t r2 = SkipRex(k);
                        if (r2 + 3 >= g_file.size() || U8(r2) != 0x8B)
                            continue;
                        unsigned char m2 = U8(r2 + 1);
                        int mo2 = (m2 >> 6) & 3;
                        if (mo2 == 0 || (m2 & 7) != 3) continue;
                        int dd = (mo2 == 1) ? (int8_t)U8(r2 + 2) : 0;
                        int dst2 = ((m2 >> 3) & 7) +
                                   ((r2 != k && (U8(k) & 4)) ? 8 : 0);
                        if (dst2 == dst && dd != topSlot && dd >= 0 &&
                            dd < 0x1000) {
                            pi.stackLast = dd;
                            break;
                        }
                    }
                }
            }
            if (pi.stackLast >= 0) break;
        }
        if (pi.stackLast >= 0)
            LogOk("StackLast=0x%x (precall grow-gate vs Top [IDA-tagged])",
                  pi.stackLast);
        else
            LogBad("MISS   lua_State.StackLast (precall grow-gate)");
    }
    
    
    
    
    
    for (size_t i = pi.entry; i + 8 < pw; ++i) {
        uint8_t rx = U8(i);
        bool bSet = (rx == 0x41 || rx == 0x43 || rx == 0x45 || rx == 0x47 || rx == 0x49 ||
                     rx == 0x4B || rx == 0x4D || rx == 0x4F);
        if (!bSet || U8(i + 1) != 0x89) continue;
        unsigned char mr = U8(i + 2);
        if (((mr >> 6) & 3) != 1 || (mr & 7) != 7) continue;
        int dp = (int8_t)U8(i + 3);
        if (dp == 0x08) pi.fFunc = 0x08;
        if (dp == 0x10) pi.fTop = 0x10;
        if (dp == 0x18) pi.fP = 0x18;
        if (dp == 0x28) pi.fNr = 0x28;
    }
    
    for (size_t i = pi.entry; i + 8 < pw; ++i) {
        uint8_t rx = U8(i);
        if ((rx == 0x49 || rx == 0x4B || rx == 0x4D || rx == 0x4F) && U8(i + 1) == 0xC7 &&
            U8(i + 2) == 0x47 && U8(i + 3) == 0x20)
            pi.fSavedpc = 0x20;
        if (rx == 0x41 && U8(i + 1) == 0xC7 && U8(i + 2) == 0x47 && U8(i + 3) == 0x2C)
            pi.fFlags = 0x2C;
    }
    
    for (size_t i = pi.entry; i + 4 < pw; ++i) {
        uint8_t rx = U8(i);
        bool bSet = (rx == 0x41 || rx == 0x43 || rx == 0x45 || rx == 0x47 || rx == 0x49 ||
                     rx == 0x4B || rx == 0x4D || rx == 0x4F);
        if (bSet && U8(i + 1) == 0x89 && U8(i + 2) == 0x27) pi.fBase = 0x00;
    }
    if (pi.fFunc < 0) LogBad("MISS   CallInfo.Func (precall)");
    if (pi.fTop < 0) LogBad("MISS   CallInfo.Top (precall)");
    if (pi.fBase < 0) LogBad("MISS   CallInfo.Base (precall)");
    if (pi.fSavedpc < 0) LogBad("MISS   CallInfo.Savedpc (precall)");
    if (pi.fNr < 0) LogBad("MISS   CallInfo.Nresults (precall)");
    if (pi.fFlags < 0) LogBad("MISS   CallInfo.Flags (precall)");
    if (pi.fP < 0) LogBad("MISS   CallInfo.P (precall CIProto)");
    
    for (size_t i = pi.entry; i + 5 < pw; ++i) {
        if (U8(i) == 0x41 && U8(i + 1) == 0x80 && U8(i + 2) == 0x7D && U8(i + 3) == 0x06 &&
            U8(i + 4) == 0x00) {
            pi.clIsC = 6;
            break;
        }
    }
    if (pi.clIsC < 0) LogBad("MISS   Closure.IsC (precall)");
    
    
    for (size_t i = pi.entry; i + 4 < pw; ++i) {
        if (U8(i) == 0x41 && U8(i + 1) == 0xFF && (U8(i + 2) == 0x53 || U8(i + 2) == 0x55)) {
            pi.clFunc = U8(i + 3);
            break;
        }
        if (U8(i) == 0x49 && U8(i + 1) == 0x8B && U8(i + 2) == 0x45) {
            int disp = U8(i + 3);
            for (size_t k = i + 4; k + 2 < pw && k < i + 16; ++k) {
                if (U8(k) == 0xFF && U8(k + 1) == 0xD0) {
                    pi.clFunc = disp;
                    break;
                }
            }
            if (pi.clFunc >= 0) break;
        }
    }
    if (pi.clFunc < 0) LogBad("MISS   Closure.C_f (precall)");
    
    
    for (size_t i = pi.entry; i + 7 < pw; ++i) {
        if (U8(i) != 0x48 || U8(i + 1) != 0x8B || U8(i + 2) != 0x88 || U8(i + 3) != 0xC0 ||
            U8(i + 4) != 0x00 || U8(i + 5) != 0x00 || U8(i + 6) != 0x00)
            continue;
        size_t lo = i > 32 ? i - 32 : pi.entry;
        for (size_t k = i; k-- > lo;) {
            if (U8(k) == 0x49 && U8(k + 1) == 0x8B && U8(k + 2) == 0x45) {
                pi.clProto = U8(k + 3);
                break;
            }
            if (k == 0) break;
        }
        if (pi.clProto >= 0) break;
    }
    if (pi.clProto < 0) LogBad("MISS   Closure.L_proto (precall deopt chain)");
    
    for (size_t i = pi.entry; i + 12 < pw; ++i) {
        uint8_t rx = U8(i);
        if (rx != 0x41 && rx != 0x4C) continue;
        if (U8(i + 1) != 0x0F || U8(i + 2) != 0xB6 || U8(i + 3) != 0x45) continue;
        int sb = U8(i + 4);
        for (size_t k = i + 5; k + 3 < pw && k < i + 16; ++k) {
            if (U8(k) == 0xC1 && (U8(k + 1) & 0xF8) == 0xE0 && U8(k + 2) == 0x04) {
                pi.clSize = sb;
                break;
            }
        }
        if (pi.clSize >= 0) break;
    }
    if (pi.clSize < 0) LogBad("MISS   Closure size byte (precall)");
    
    
    {
        size_t win = (size_t)resumeFn + 1024;
        if (win > g_file.size()) win = g_file.size();
        for (size_t i = resumeFn; i + 5 < win; ++i) {
            size_t q = SkipRex(i);
            if (q + 4 >= g_file.size() || U8(q) != 0x80) continue;
            unsigned char mr = U8(q + 1);
            if (((mr >> 6) & 3) != 1 || ((mr >> 3) & 7) != 7) continue;
            if (U8(q + 2) != 0x06 || U8(q + 3) != 0x00) continue;
            for (size_t k = i; k + 6 < win && k < i + 64; ++k) {
                size_t r = SkipRex(k);
                if (r + 5 >= g_file.size() || U8(r) != 0x83) continue;
                unsigned char m2 = U8(r + 1);
                if (((m2 >> 6) & 3) != 1 || ((m2 >> 3) & 7) != 7 || (m2 & 7) == 4) continue;
                if (U8(r + 2) == 0x20 && U8(r + 3) == 0x00) {
                    pi.clCont = 0x20;
                    break;
                }
            }
            break;
        }
        if (pi.clCont < 0) LogBad("MISS   Closure.C_cont (resume trailer)");
    }
    return pi.ci >= 0 && pi.fFunc >= 0 && pi.fTop >= 0;
}

struct TableInfo {
    uint64_t entry = 0, hashFn = 0, errFn = 0;
    int sizeArray = -1, node = -1, array = -1, tT = -1, tV = -1, tE = -1;
    int meta = -1;
};

static bool LuauTable(TableInfo& ti) {
    auto strs = FindStrings("invalid key to 'next'");
    if (strs.empty()) {
        LogBad("MISS   Luau luaH_next (no string)");
        return false;
    }
    auto refs = StringRefs(strs[0]);
    uint64_t entry = 0;
    size_t refSite = 0;
    for (size_t r : refs) {
        entry = FuncEntry(r);
        if (entry && InText(entry)) {
            refSite = r;
            break;
        }
        entry = 0;
    }
    if (!entry) {
        LogBad("MISS   Luau luaH_next (no entry)");
        return false;
    }
    ti.entry = entry;
    LogInfo("luaH_next=%#lx", (unsigned long)entry);
    size_t win = (size_t)entry + 1024;
    if (win > g_file.size()) win = g_file.size();
    
    for (size_t i = refSite; i + 5 < win; ++i) {
        uint64_t t = CallTarget(i);
        if (t) {
            ti.errFn = t;
            break;
        }
    }
    
    for (size_t i = entry; i + 5 < win; ++i) {
        uint64_t t = CallTarget(i);
        if (!t || t == ti.errFn) continue;
        for (size_t k = t; k + 3 < t + 640 && k + 3 < g_file.size(); ++k) {
            if (U8(k) == 0xC1 && (U8(k + 1) & 0xF8) == 0xE0 && U8(k + 2) == 0x05) {
                ti.hashFn = t;
                break;
            }
        }
        if (ti.hashFn) break;
    }
    if (ti.hashFn) LogInfo("luaH_hash=%#lx", (unsigned long)ti.hashFn);
    
    int votes[8], nv = 0;
    for (size_t i = entry; i + 3 < win && nv < 8; ++i) {
        size_t q = SkipRex(i);
        if (q + 2 >= g_file.size() || U8(q) != 0x8B) continue;
        if ((U8(q + 1) & 0xC7) != 0x46) continue;
        votes[nv++] = (int8_t)U8(q + 2);
        i = q + 2;
    }
    if (nv >= 2 && votes[0] == votes[1]) ti.sizeArray = votes[0];
    if (ti.sizeArray < 0) LogBad("MISS   Table.Sizearray (luaH_next)");
    
    bool sawShl5 = false;
    for (size_t i = entry; i + 8 < win; ++i) {
        if (U8(i) == 0xC1 && (U8(i + 1) & 0xF8) == 0xE0 && U8(i + 2) == 0x05) sawShl5 = true;
        if (!sawShl5) continue;
        size_t q = SkipRex(i);
        if (q + 3 >= g_file.size() || U8(q) != 0x2B) continue;
        if ((U8(q + 1) & 0xC7) != 0x46) continue;
        ti.node = (int8_t)U8(q + 2);
        break;
    }
    if (ti.node < 0) LogBad("MISS   Table.Node (luaH_next)");
    
    
    
    
    
    for (size_t i = entry; i + 4 < win; ++i) {
        size_t q = SkipRex(i);
        if (q + 3 >= g_file.size() || U8(q) != 0x8B) continue;
        if ((U8(q + 1) & 0xC7) != 0x46) continue;
        int cand = (int8_t)U8(q + 2);
        if (cand == ti.sizeArray || cand == ti.node) continue;
        for (size_t k = i; k + 10 < win && k < i + 64; ++k) {
            size_t r = SkipRex(k);
            if (r + 6 >= g_file.size() || U8(r) != 0x83) continue;
            unsigned char mr = U8(r + 1);
            if (((mr >> 6) & 3) != 1 || ((mr >> 3) & 7) != 7 || (mr & 7) != 4) continue;
            size_t dd = r + 3;  
            if (U8(dd) != 0x1C || U8(dd + 1) != 0x00) continue;
            ti.array = cand;
            break;
        }
        if (ti.array >= 0) break;
    }
    if (ti.array < 0) LogBad("MISS   Table.Array (luaH_next)");
    
    for (size_t i = entry; i + 3 < win; ++i) {
        size_t q = SkipRex(i);
        if (q + 2 >= g_file.size() || U8(q) != 0x8B || U8(q + 1) != 0x42 || U8(q + 2) != 0x0C)
            continue;
        ti.tT = 0x0C;
        break;
    }
    if (ti.tT < 0) LogBad("MISS   TValue.Tt (luaH_next)");
    return ti.sizeArray >= 0 && ti.node >= 0 && ti.array >= 0;
}



static uint64_t LuauSettable(TableInfo& ti) {
    auto strs = FindStrings("table index is nil");
    if (strs.empty()) {
        LogBad("MISS   Luau settable (no string)");
        return 0;
    }
    auto refs = StringRefs(strs[0]);
    uint64_t entry = 0;
    for (size_t r : refs) {
        entry = FuncEntry(r);
        if (entry && InText(entry)) break;
        entry = 0;
    }
    if (!entry) {
        LogBad("MISS   Luau settable (no entry)");
        return 0;
    }
    LogInfo("settable=%#lx", (unsigned long)entry);
    size_t win = (size_t)entry + 512;
    if (win > g_file.size()) win = g_file.size();
    bool tt = false, val0 = false, ex8 = false;
    for (size_t i = entry; i + 4 < win; ++i) {
        size_t q = SkipRex(i);
        if (q + 3 >= g_file.size()) continue;
        
        if (U8(q) == 0x8B && U8(q + 1) == 0x42 && U8(q + 2) == 0x0C) tt = true;
        
        if (U8(q) == 0xF3 && U8(q + 1) == 0x0F && U8(q + 2) == 0x10 &&
            ((U8(q + 3) >> 6) & 3) == 0)
            val0 = true;
        
        if (U8(q) == 0xF3 && U8(q + 1) == 0x0F && U8(q + 2) == 0x10 && U8(q + 3) == 0x42 &&
            U8(q + 4) == 0x08)
            ex8 = true;
    }
    if (tt) ti.tT = 0x0C;
    if (val0) ti.tV = 0x00;
    if (ex8) ti.tE = 0x08;
    if (tt)
        LogOk("TValue layout (settable vector lanes + tt check)");
    else
        LogBad("MISS   TValue layout (settable)");
    return entry;
}


static bool LuauMetatable(TableInfo& ti) {
    auto strs = FindStrings("'__index' chain too long; possible loop");
    if (strs.empty()) {
        LogBad("MISS   metatable chain string");
        return false;
    }
    auto refs = StringRefs(strs[0]);
    uint64_t entry = 0;
    for (size_t r : refs) {
        entry = FuncEntry(r);
        if (entry && InText(entry)) break;
        entry = 0;
    }
    if (!entry) {
        LogBad("MISS   metatable chain fn");
        return false;
    }
    size_t win = (size_t)entry + 512;
    if (win > g_file.size()) win = g_file.size();
    for (size_t i = entry; i + 6 < win; ++i) {
        size_t q = SkipRex(i);
        if (q + 3 >= g_file.size() || U8(q) != 0x8B) continue;
        unsigned char mr = U8(q + 1);
        if (((mr >> 6) & 3) != 1 || (mr & 7) == 4) continue;
        int mt = (int8_t)U8(q + 2);
        int dst = (mr >> 3) & 7;
        if (U8(i) != 0x48 && U8(i) != 0x49 && U8(i) != 0x4C && U8(i) != 0x4D &&
            q != i)  
            continue;
        bool dstExt = (q != i) && (U8(i) & 1);
        int full = dst + (dstExt ? 8 : 0);
        for (size_t k = i + 3; k + 3 < win && k < i + 16; ++k) {
            size_t r = SkipRex(k);
            if (r + 2 >= g_file.size() || U8(r) != 0x85) continue;
            unsigned char m2 = U8(r + 1);
            if (((m2 >> 6) & 3) != 3) continue;
            int d2 = (m2 >> 3) & 7, s2 = m2 & 7;
            bool ext2 = (r != k) && (U8(k) & 1);
            if (d2 + (ext2 ? 8 : 0) == full && (s2 + (ext2 ? 8 : 0)) == full) {
                ti.meta = mt;
                LogInfo("Metatable=0x%x (chain nil-check)", mt);
                goto anchored;
            }
        }
    }
anchored:
    
    
    
    
    
    if (ti.meta != 0x28) {
        LogBad("Metatable override 0x%x -> 0x28 [IDA sub_26F798F miss-path nil-check]",
               ti.meta >= 0 ? (unsigned)ti.meta : 0);
        ti.meta = 0x28;
    } else {
        LogOk("Metatable=0x28 agrees with IDA anchor");
    }
    return true;
}

}  


namespace {

static std::string FileMd5(const char* path) {
    std::string cmd = std::string("md5sum '") + path + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "unknown";
    char b[64] = {};
    size_t n = fread(b, 1, sizeof(b) - 1, p);
    pclose(p);
    std::string s(b, n);
    if (s.size() >= 32 && s.substr(0, 32).find_first_not_of("0123456789abcdef") == std::string::npos)
        return s.substr(0, 32);
    return "unknown";
}

static std::string ApkVersion() {
    const char* home = getenv("HOME");
    if (!home) return "unknown";
    std::string apk = std::string(home) +
                      "/.var/app/org.vinegarhq.Sober/data/sober/packages/"
                      "x86_64/com.roblox.client/base.apk";
    FILE* t = fopen(apk.c_str(), "r");
    if (!t) return "unknown";
    fclose(t);
    std::string out;
    FILE* p = popen(("aapt dump badging \"" + apk + "\" 2>/dev/null | grep versionName").c_str(),
                    "r");
    if (!p) return "unknown";
    char b[512];
    while (fgets(b, sizeof(b), p)) out += b;
    pclose(p);
    size_t q = out.find("versionName='");
    if (q == std::string::npos) return "unknown";
    q += 13;
    size_t e = out.find('\'', q);
    if (e == std::string::npos || e <= q) return "unknown";
    return out.substr(q, e - q);
}

void WriteLuauHpp() {
    std::string md5 = FileMd5(g_dumpedPath.c_str());
    std::string apkVer = ApkVersion();
    std::string base = g_dumpedPath;
    size_t sl = base.find_last_of('/');
    if (sl != std::string::npos) base = base.substr(sl + 1);
    
    std::string prevMd5;
    {
        FILE* o = fopen("luau_offsets.hpp", "r");
        if (o) {
            char ln[512];
            while (fgets(ln, sizeof(ln), o)) {
                std::string s = ln;
                size_t p = s.find("(md5 ");
                if (p != std::string::npos && s.size() >= p + 5 + 32) {
                    std::string m = s.substr(p + 5, 32);
                    if (m.find_first_not_of("0123456789abcdef") == std::string::npos)
                        prevMd5 = m;
                    break;
                }
            }
            fclose(o);
        }
    }
    if (!prevMd5.empty() && prevMd5 != md5) {
        LogBad("luau version changed (md5 %s -> %s): NOT carrying old offsets", prevMd5.c_str(),
               md5.c_str());
    } else {
        FILE* o = fopen("luau_offsets.hpp", "r");
        if (o) {
            char ln[512];
            std::string cat;
            int kept = 0;
            struct KE {
                std::string cat, name;
                uintptr_t val;
            };
            std::vector<KE> old;
            while (fgets(ln, sizeof(ln), o)) {
                std::string s = ln;
                size_t np = s.find("namespace ");
                if (np != std::string::npos) {
                    size_t e = s.find_first_of(" {", np + 10);
                    cat = s.substr(np + 10, e - np - 10);
                    continue;
                }
                unsigned long v = 0;
                char nm[128] = {};
                if (sscanf(s.c_str(), " inline constexpr uintptr_t %127s = 0x%lx", nm, &v) == 2) {
                    std::string n = nm;
                    old.push_back({cat, n, (uintptr_t)v});
                }
            }
            fclose(o);
            for (auto& e : old) {
                bool have = false;
                for (auto& g : g_emitted)
                    if (g.cat == e.cat && g.name == e.name) {
                        have = true;
                        break;
                    }
                if (!have) {
                    g_emitted.push_back({e.cat, e.name, "kept-old", e.val});
                    LogInfo("KEPT-OLD %-28s %-14s 0x%lx", e.name.c_str(), e.cat.c_str(),
                            (unsigned long)e.val);
                    ++kept;
                }
            }
            if (kept) LogInfo("KEPT-OLD total %d offsets from previous header", kept);
        }
    }
    FILE* f = fopen("luau_offsets.hpp", "w");
    if (!f) {
        LogBad("could not write luau_offsets.hpp");
        return;
    }
    
    fprintf(f, "//dumped by idom - ix dumper v2.3\n");
    fprintf(f, "//build: %s (md5 %s)\n", base.c_str(), md5.c_str());
    fprintf(f, "//apk version: %s\n", apkVer.c_str());
    fprintf(f, "//discord: _idom_\n");
    fprintf(f, "//offsets found: %zu\n", g_emitted.size());
    fprintf(f, "#pragma once\n\n#include <cstdint>\n\nnamespace Offsets {\n");
    std::vector<std::string> order;
    for (auto& e : g_emitted)
        if (std::find(order.begin(), order.end(), e.cat) == order.end()) order.push_back(e.cat);
    for (size_t ci = 0; ci < order.size(); ++ci) {
        fprintf(f, "    namespace %s {\n", order[ci].c_str());
        for (auto& e : g_emitted) {
            if (e.cat != order[ci]) continue;
            fprintf(f, "        inline constexpr uintptr_t %s = 0x%lx;\n", e.name.c_str(),
                    (unsigned long)e.val);
        }
        fprintf(f, "    }%s", ci + 1 < order.size() ? "\n\n" : "\n");
    }
    fprintf(f, "}\n");
    fclose(f);
    {
        char cwd[4096] = {};
        if (!getcwd(cwd, sizeof(cwd) - 1)) cwd[0] = 0;
        LogOk("wrote %s/luau_offsets.hpp (%zu offsets)", cwd, g_emitted.size());
    }
    
    
}

static void SeleneCarry(const char* name, const char* cat, uintptr_t off) {
    LogBad("selene-carry %-22s %-14s 0x%lx (UNVERIFIED on this build)", name, cat,
           (unsigned long)off);
    Emit(name, cat, off, "selene-carry");
}

static bool CopyFileLuau(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    out << in.rdbuf();
    out.flush();
    return (bool)out;
}

}  





void PublishLuau() {
    const char* noPub = getenv("IX_NO_PUBLISH");
    const char* home = getenv("HOME");
    if (noPub && noPub[0]) {
        LogInfo("[publish:luau] skipped (IX_NO_PUBLISH set)");
        return;
    }
    if (!home) {
        LogBad("[publish:luau] skipped (no HOME)");
        return;
    }
    std::string md5 = FileMd5(g_dumpedPath.c_str());
    std::string apkVer = ApkVersion();
    std::string upDir = std::string(home) + "/offsets_upload";
    char cwd2[4096] = {};
    std::string cwd = getcwd(cwd2, sizeof(cwd2) - 1) ? std::string(cwd2) : ".";
    bool ok = true;
    if (!CopyFileLuau(cwd + "/luau_offsets.hpp", upDir + "/luau_offsets.hpp")) {
        LogBad("[publish:luau] copy failed (luau_offsets.hpp)");
        ok = false;
    }
    std::string luauPath;  
    if (ok && apkVer != "unknown") {
        std::string vdir = upDir + "/version/" + apkVer;
        if (system(("mkdir -p \"" + vdir + "\"").c_str()) != 0) {
            LogBad("[publish:luau] mkdir failed (%s)", vdir.c_str());
            ok = false;
        } else if (!CopyFileLuau(cwd + "/luau_offsets.hpp", vdir + "/luau_offsets.hpp")) {
            LogBad("[publish:luau] archive copy failed (%s)", vdir.c_str());
            ok = false;
        } else {
            luauPath = "version/" + apkVer + "/luau_offsets.hpp";
            LogOk("[publish:luau] archived %s", (upDir + "/" + luauPath).c_str());
        }
    } else if (ok) {
        LogBad("[publish:luau] unknown apk: root copy only, no archive/versions entry");
    }
    if (ok && !luauPath.empty()) {
        
        
        
        
        std::string vpath = upDir + "/versions";
        struct VEnt {
            std::string apk, md5, path, st, luau, ff;
            size_t count = 0;
        };
        std::vector<VEnt> ents;
        std::vector<std::string> other;
        {
            FILE* vf = fopen(vpath.c_str(), "r");
            if (vf) {
                char ln[512];
                std::string blk;
                auto flush = [&]() {
                    if (blk.empty()) return;
                    VEnt e;
                    char b1[64] = {}, b2[64] = {}, b3[256] = {}, b4[256] = {}, b5[256] = {},
                         b6[256] = {};
                    auto grab = [&](const char* key, char* dst, size_t n) {
                        std::string k = std::string("\"") + key + "\"";
                        size_t p = blk.find(k);
                        if (p == std::string::npos) return false;
                        p = blk.find('"', p + k.size());
                        if (p == std::string::npos) return false;
                        size_t q = blk.find('"', p + 1);
                        if (q == std::string::npos) return false;
                        snprintf(dst, n, "%s", blk.substr(p + 1, q - p - 1).c_str());
                        return true;
                    };
                    bool hasApk = grab("apk", b1, sizeof(b1));
                    grab("md5", b2, sizeof(b2));
                    grab("path", b3, sizeof(b3));
                    grab("struct", b4, sizeof(b4));
                    grab("luau", b5, sizeof(b5));
                    grab("fflags", b6, sizeof(b6));
                    size_t cnt = 0;
                    size_t pc2 = blk.find("\"offsets\"");
                    if (pc2 != std::string::npos) {
                        size_t cc = blk.find(':', pc2);
                        if (cc != std::string::npos) cnt = strtoul(blk.c_str() + cc + 1, nullptr, 10);
                    }
                    if (hasApk) {
                        e.apk = b1;
                        e.md5 = b2;
                        e.path = b3;
                        e.st = b4;
                        e.luau = b5;
                        e.ff = b6;
                        e.count = cnt;
                        ents.push_back(e);
                    } else if (blk.find('[') == std::string::npos &&
                               blk.find(']') == std::string::npos) {
                        other.push_back(blk);
                    }
                    blk.clear();
                };
                while (fgets(ln, sizeof(ln), vf)) {
                    std::string s = ln;
                    if (s.find('{') != std::string::npos && !blk.empty()) flush();
                    if (s.find('{') != std::string::npos || !blk.empty() || s.find('}') != std::string::npos)
                        blk += s;
                    if (s.find('}') != std::string::npos) flush();
                }
                flush();
                fclose(vf);
            }
        }
        bool attached = false;
        for (auto& e : ents) {
            if (e.apk == apkVer || (!md5.empty() && e.md5 == md5 && md5 != "unknown")) {
                e.luau = luauPath;
                attached = true;
                break;
            }
        }
        if (!attached) {
            VEnt cur{apkVer, md5, "", "", luauPath, "", g_emitted.size()};
            ents.insert(ents.begin(), cur);
            LogBad("[publish:luau] no versions entry for apk %s: luau-only placeholder "
                   "(run --step all to fill path/offsets)",
                   apkVer.c_str());
        }
        {
            FILE* vf = fopen(vpath.c_str(), "w");
            if (!vf) {
                LogBad("[publish:luau] cannot write versions index");
                ok = false;
            } else {
                fprintf(vf, "[\n");
                for (size_t i = 0; i < ents.size(); ++i) {
                    auto& e = ents[i];
                    fprintf(vf, "  {\n");
                    fprintf(vf, "    \"apk\": \"%s\",\n", e.apk.c_str());
                    fprintf(vf, "    \"md5\": \"%s\",\n", e.md5.c_str());
                    fprintf(vf, "    \"offsets\": %zu,\n", e.count);
                    fprintf(vf, "    \"path\": \"%s\"%s\n", e.path.c_str(),
                            (e.st.empty() && e.luau.empty() && e.ff.empty()) ? "" : ",");
                    if (!e.st.empty())
                        fprintf(vf, "    \"struct\": \"%s\"%s\n", e.st.c_str(),
                                (e.luau.empty() && e.ff.empty()) ? "" : ",");
                    if (!e.luau.empty())
                        fprintf(vf, "    \"luau\": \"%s\"%s\n", e.luau.c_str(),
                                e.ff.empty() ? "" : ",");
                    if (!e.ff.empty()) fprintf(vf, "    \"fflags\": \"%s\"\n", e.ff.c_str());
                    fprintf(vf, "  }%s\n", i + 1 < ents.size() ? "," : "");
                }
                for (auto& o : other) fputs(o.c_str(), vf);
                fprintf(vf, "]\n");
                fclose(vf);
                LogOk("[publish:luau] versions index updated (%zu entries)", ents.size());
            }
        }
    }
    if (!ok) return;
    char msg[160];
    snprintf(msg, sizeof(msg), "Update luau offsets (%zu offsets, apk %s)", g_emitted.size(),
             apkVer.c_str());
    std::string files = "luau_offsets.hpp versions";
    if (!luauPath.empty()) files += " " + luauPath;
    std::string cmd = "git -C \"" + upDir + "\" diff --quiet -- " + files +
                      " 2>/dev/null || (git -C \"" + upDir + "\" add " + files + " && git -C \"" +
                      upDir + "\" commit -qm \"" + msg + "\" && GIT_TERMINAL_PROMPT=0 git -C \"" +
                      upDir + "\" push 2>&1)";
    int rc = system(cmd.c_str());
    if (rc == 0)
        LogOk("[publish:luau] website up to date / pushed");
    else
        LogBad("[publish:luau] git push failed (rc=%d)", rc);
}

bool Luau() {
    if (!LoadFile()) return false;
    LogInfo("luau static on %s (.text %#lx+%#lx)", g_dumpedPath.c_str(), (unsigned long)g_t0,
            (unsigned long)g_tsz);
    ResumeInfo ri;
    bool okResume = LuauResume(ri);
    if (okResume) {
        ParseResumeError(ri);
        ParseNewStr(ri);
        ParseRealloc(ri);
    }
    PrecallInfo pi;
    LuauPrecall(pi, ri.top);
    TableInfo ti;
    LuauTable(ti);
    uint64_t setFn = LuauSettable(ti);
    LuauMetatable(ti);
    
    if (ti.errFn) {
        
        auto strs = FindStrings("table index is nil");
        if (!strs.empty()) {
            auto refs = StringRefs(strs[0]);
            for (size_t r : refs) {
                size_t win = r + 256;
                if (win > g_file.size()) win = g_file.size();
                for (size_t i = r; i + 5 < win; ++i) {
                    uint64_t t = CallTarget(i);
                    if (t) {
                        if (t == ti.errFn)
                            LogOk("RunError=%#lx (next+settable agree)", (unsigned long)t);
                        else
                            LogInfo("runerror next=%#lx settable=%#lx (differ)", (unsigned long)ti.errFn,
                                    (unsigned long)t);
                        Emit("RunError", "LuaFunctions", t, "func");
                        break;
                    }
                }
                break;
            }
        }
    }
    
    if (ri.ciA >= 0 && pi.ci >= 0) {
        if (pi.ci == ri.ciA || pi.ci == ri.ciB)
            LogOk("Ci=0x%x (resume pair + precall store agree)", pi.ci);
        else
            LogBad("Ci CONFLICT: resume pair {0x%llx,0x%llx} vs precall 0x%x",
                   (unsigned long long)ri.ciA, (unsigned long long)ri.ciB, pi.ci);
    }

    auto E = [](const char* n, const char* c, int64_t v, const char* ty) {
        if (v >= 0) Emit(n, c, (uintptr_t)v, ty);
    };
    
    
    if (ri.memcat >= 0 && ri.mMarked >= 0) {
        if (ri.memcat == ri.mMarked)
            LogOk("Marked=0x%x (barrier bit + init triple agree)", ri.mMarked);
        else
            LogBad("Marked CONFLICT: barrier 0x%x vs triple 0x%x", ri.memcat, ri.mMarked);
    }
    E("Tt", "lua_State", ri.mTt, "uint8_t");
    E("Marked", "lua_State", ri.mMarked, "uint8_t");
    E("Memcat", "lua_State", ri.mMemcat, "uint8_t");
    E("Status", "lua_State", ri.status, "uint8_t");
    E("IsActive", "lua_State", ri.isActive, "uint8_t");
    E("Singlestep", "lua_State", ri.isActive >= 0 ? 0x05 : -1, "inferred");
    E("NCcalls", "lua_State", ri.ncCalls, "uint16_t");
    E("BaseCcalls", "lua_State", ri.baseCCalls, "uint16_t");
    E("Stacksize", "lua_State", ri.stackSize, "uint32_t");
    E("EndCi", "lua_State", pi.endCi, "unsigned __int64");
    E("BaseCi", "lua_State",
      (ri.ciA >= 0 && pi.ci >= 0) ? (pi.ci == (int)ri.ciA ? ri.ciB : ri.ciA) : -1, "likely");
    E("Ci", "lua_State", pi.ci, "unsigned __int64");
    E("Base", "lua_State", ri.baseField, "unsigned __int64");
    E("Stack", "lua_State", ri.stackBase, "unsigned __int64");
    E("StackLast", "lua_State",
      pi.stackLast >= 0 ? pi.stackLast : ri.stackLast, "unsigned __int64");
    if (pi.stackLast >= 0 && ri.stackLast < 0)
        LogInfo("StackLast via precall (resume_error shape missed)");
    E("OpenUpval", "lua_State", ri.openUpval, "unsigned __int64");
    E("Gclist", "lua_State", ri.gclist, "unsigned __int64");
    E("GlobalState", "lua_State", ri.gGlobal >= 0 ? ri.gGlobal : ri.globalSlot, "unsigned __int64");
    E("Top", "lua_State", ri.top, "unsigned __int64");
    E("Activememcat", "lua_State", (ri.mTt >= 0) ? 0x04 : -1, "inferred");
    if (ri.status < 0 || pi.ci < 0 || ri.top < 0)
        LogBad("lua_State PARTIAL (see MISS lines above)");
    else
        LogOk("lua_State complete");
    
    E("Strt", "global_State", ri.strt, "unsigned __int64");
    E("Currentwhite", "global_State", ri.gWhite, "uint8_t");
    if (ri.strt < 0) LogBad("global_State PARTIAL (rest: Frealloc/Ud/Gcstate/Gray/Grayagain/Weak/"
                            "GCthreshold/Totalbytes/Mainthread/Registry MISS)");
    
    E("Value", "TValue", ti.tV, "uint64_t");
    E("Extra", "TValue", ti.tE, "uint32_t");
    E("Tt", "TValue", ti.tT, "int32_t");
    if (ti.tV < 0 || ti.tE < 0 || ti.tT < 0) LogBad("TValue PARTIAL");
    
    E("Base", "CallInfo", pi.fBase, "unsigned __int64");
    E("Top", "CallInfo", pi.fTop, "unsigned __int64");
    E("Func", "CallInfo", pi.fFunc, "unsigned __int64");
    E("P", "CallInfo", pi.fP, "unsigned __int64");
    E("Savedpc", "CallInfo", pi.fSavedpc, "unsigned __int64");
    E("Nresults", "CallInfo", pi.fNr, "int32_t");
    E("Flags", "CallInfo", pi.fFlags, "uint32_t");
    if (pi.ci >= 0) Emit("Size", "CallInfo", 0x30, "stride");
    
    E("IsC", "Closure", pi.clIsC, "uint8_t");
    E("Stacksize", "Closure", pi.clSize, "uint8_t");
    E("C_f", "Closure", pi.clFunc, "unsigned __int64");
    E("L_proto", "Closure", pi.clProto, "unsigned __int64");
    E("C_cont", "Closure", pi.clCont, "unsigned __int64");
    SeleneCarry("Gclist", "Closure", 0x08);
    SeleneCarry("Env", "Closure", 0x10);
    SeleneCarry("L_uprefs", "Closure", 0x20);
    SeleneCarry("C_debugname", "Closure", 0x28);
    SeleneCarry("C_upvals", "Closure", 0x30);
    
    E("Sizearray", "Table", ti.sizeArray, "int32_t");
    E("Metatable", "Table", ti.meta, "unsigned __int64");
    E("Node", "Table", ti.node, "unsigned __int64");
    E("Array", "Table", ti.array, "unsigned __int64");
    E("Lastfree", "Table", 0x0C, "selene-carry");
    LogBad("selene-carry Lastfree Table 0x%lx (UNVERIFIED on this build)", 0x0Cul);
    LogBad("Table Tmcache/Readonly/Safeenv/Nodemask8/Gclist: MISS (header compacted, "
           "master spots invalid)");
    if (ti.sizeArray < 0 || ti.node < 0 || ti.array < 0) LogBad("Table PARTIAL");
    
    const struct {
        const char* n;
        uintptr_t v;
    } protoCarry[] = {
        {"Tt", 0x01},          {"Memcat", 0x00},     {"Marked", 0x02},    {"Maxstacksize", 0x03},
        {"K", 0x08},           {"Code", 0x10},       {"P", 0x18},         {"Codeentry", 0x20},
        {"Execdata", 0x28},    {"Exectarget", 0x30}, {"Lineinfo", 0x38},  {"Locvars", 0x48},
        {"Upvalues", 0x50},    {"Source", 0x58},     {"Debugname", 0x60}, {"Sizecode", 0x88},
        {"Sizep", 0x8c},       {"Sizek", 0x98},
    };
    for (auto& pc : protoCarry) SeleneCarry(pc.n, "Proto", pc.v);
    LogBad("Proto Nups/Numparams/Is_vararg/Flags: MISS (maxstacksize@3 finding voids master spots)");
    
    if (ri.entry) Emit("ResumeStart", "LuaFunctions", ri.entry, "func");
    if (ti.entry) Emit("LuaHNext", "LuaFunctions", ti.entry, "func");
    if (ti.hashFn) Emit("LuaHHash", "LuaFunctions", ti.hashFn, "func");
    if (pi.entry) Emit("LuauPrecall", "LuaFunctions", pi.entry, "func");
    if (ri.growFn) Emit("ReallocStack", "LuaFunctions", ri.growFn, "func");
    if (ri.newStrFn) Emit("NewStr", "LuaFunctions", ri.newStrFn, "func");
    if (ri.errFn) Emit("ResumeError", "LuaFunctions", ri.errFn, "func");
    if (pi.resumeFn) Emit("Resume", "LuaFunctions", pi.resumeFn, "func");
    if (setFn) Emit("SetTable", "LuaFunctions", setFn, "func");

    WriteLuauHpp();
    bool crit = (ri.status >= 0 && pi.ci >= 0 && ri.top >= 0 && ti.sizeArray >= 0 && ti.node >= 0 &&
                 ti.array >= 0);
    if (crit)
        LogOk("LUAU FLOW OK");
    else
        LogBad("LUAU FLOW FAILED");
    return crit;
}

}  
