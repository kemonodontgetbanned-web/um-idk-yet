#include "dump.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "globals.h"
#include "mem.h"
#include "roblox.h"
#include "rtti.h"
#include "scanner.h"
#include "log.h"

namespace Dump {

bool ReadFileBytes(std::vector<char>& out) {
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

bool FpsValid(uintptr_t sched, uintptr_t& fpsOff) {
    
    
    
    for (int pass = 0; pass < 2; ++pass) {
        for (uintptr_t i = 0; i < 0x100; i += 1) {
            double v = Read<double>(sched + i);
            if (v == 0 || !std::isfinite(v)) continue;
            bool ok = false;
            if (pass == 0) {
                double tol = g_maxFps > 1000 ? g_maxFps * 0.01 : 1.0;
                if (fabs(v - g_maxFps) < tol) ok = true;
                if (fabs(1.0 / v - g_maxFps) < tol) ok = true;
            } else {
                if (v >= 5e-6 && v <= 0.05) ok = true;  
                if (v == floor(v) && v >= 20 && v <= 200000) ok = true;
            }
            if (ok) {
                fpsOff = i;
                return true;
            }
        }
    }
    return false;
}


bool RipDecode(const std::vector<char>& d, size_t pos, size_t lim,
               uint64_t& target, bool& isStore) {
    auto u8 = [&](size_t i) -> unsigned char {
        return static_cast<unsigned char>(d[i]);
    };
    uint64_t t = 0;
    bool store = false, ok = false;
    if (pos + 7 <= lim && u8(pos) >= 0x48 && u8(pos) <= 0x4F &&
        (u8(pos + 1) == 0x8D || u8(pos + 1) == 0x8B ||
         u8(pos + 1) == 0x89) &&
        ModRmRip(u8(pos + 2))) {
        int32_t disp;
        memcpy(&disp, &d[pos + 3], 4);
        t = pos + 7 + (int64_t)disp;
        store = (u8(pos + 1) == 0x89);
        ok = true;
    } else if (pos + 6 <= lim &&
               (u8(pos) == 0x8D || u8(pos) == 0x8B || u8(pos) == 0x89) &&
               ModRmRip(u8(pos + 1))) {
        int32_t disp;
        memcpy(&disp, &d[pos + 2], 4);
        t = pos + 6 + (int64_t)disp;
        store = (u8(pos) == 0x89);
        ok = true;
    }
    if (!ok) return false;
    target = t;
    isStore = store;
    return true;
}



uintptr_t DeriveSchedulerPointer(uintptr_t rx,
                                 const std::vector<char>& file) {
    const char needle[] = "Default job arbiter must always be valid";
    const size_t nlen = sizeof(needle) - 1;
    size_t off = 0;
    while (off + nlen <= file.size()) {
        const void* p =
            memmem(file.data() + off, file.size() - off, needle, nlen);
        if (!p) break;
        size_t s = (const char*)p - file.data();
        off = s + 1;
        
        
        for (size_t i = 0; i + 7 < file.size(); ++i) {
            uint64_t tgt = 0;
            bool dummy = false;
            if (!RipDecode(file, i, file.size(), tgt, dummy)) continue;
            if (tgt != s) continue;
            
            size_t lo = i > 0x600 ? i - 0x600 : 0;
            size_t hi = i + 0x600 < file.size() ? i + 0x600 : file.size();
            for (size_t k = lo; k + 7 < hi; ++k) {
                uint64_t g = 0;
                bool store = false;
                if (!RipDecode(file, k, hi, g, store) || !store) continue;
                uintptr_t sched = Read<uintptr_t>(rx + g);
                if (!IsValid(sched)) continue;
                uintptr_t dummyOff = 0;
                if (FpsValid(sched, dummyOff)) return g;
            }
        }
    }
    return 0;
}








static bool MatFinite4(const float* m) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i]) || fabsf(m[i]) > 1e6f) return false;
    return true;
}

static bool MatOrtho4(const float* m) {
    
    auto dot = [](const float* a, const float* b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const float* r[3] = {m, m + 4, m + 8};
    for (int i = 0; i < 3; ++i) {
        float l = dot(r[i], r[i]);
        if (!(l > 0.98f && l < 1.02f)) return false;
    }
    if (fabsf(dot(r[0], r[1])) > 0.02f) return false;
    if (fabsf(dot(r[0], r[2])) > 0.02f) return false;
    if (fabsf(dot(r[1], r[2])) > 0.02f) return false;
    return true;
}



static std::vector<uintptr_t> FindVeSlots(const std::vector<char>& file) {
    std::vector<uintptr_t> out;
    size_t n = file.size();
    auto u8 = [&](size_t i) -> unsigned char {
        return static_cast<unsigned char>(file[i]);
    };
    for (size_t i = 0; i + 21 <= n; ++i) {
        if (u8(i) != 0x48 || u8(i + 1) != 0x8B || u8(i + 2) != 0x05)
            continue;
        if (u8(i + 7) != 0x48 || u8(i + 8) != 0x85 || u8(i + 9) != 0xC0)
            continue;
        if (u8(i + 10) != 0x74) continue;
        if (u8(i + 12) != 0x48 || u8(i + 13) != 0x8B ||
            u8(i + 14) != 0x3D)
            continue;
        if (u8(i + 19) != 0xFF || u8(i + 20) != 0xD0) continue;
        int32_t d1, d2;
        memcpy(&d1, &file[i + 3], 4);
        memcpy(&d2, &file[i + 15], 4);
        uint64_t fn = i + 7 + (int64_t)d1;
        uint64_t ve = (i + 12) + 7 + (int64_t)d2;
        if (ve == fn + 8) out.push_back((uintptr_t)ve);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static void ValidateImports(uintptr_t rx) {
    
    
    
    
    
    
    LogInfo("SKIP FakeDataModel::Pointer (no image static on this build; use RenderJob chain)");
    
    if (DetectResolution())
        LogInfo("window resolution: %dx%d (xrandr)", g_winW, g_winH);
    std::vector<char> file;
    std::vector<uintptr_t> slots;
    if (ReadFileBytes(file)) slots = FindVeSlots(file);
    LogInfo("VE-callback pattern hits: %zu", slots.size());
    uintptr_t t0 = 0, tsz = 0;
    TextFileRange(t0, tsz);
    for (uintptr_t slot : slots) {
        uintptr_t fn = Read<uintptr_t>(rx + slot - 8);
        uintptr_t ve = Read<uintptr_t>(rx + slot);
        if (!IsValid(ve)) continue;
        if (!(tsz && fn >= rx && fn < rx + tsz)) {
            LogInfo("VE slot %#lx rejected (fnslot not in .text)",
                    (unsigned long)slot);
            continue;
        }
        
        uintptr_t dimOff = 0;
        int dimCount = 0;
        {
            char blk[0x2000];
            if (ReadBytes(ve, blk, sizeof(blk))) {
                for (uintptr_t i = 0; i + 8 <= sizeof(blk); i += 4) {
                    float w, h;
                    memcpy(&w, blk + i, 4);
                    memcpy(&h, blk + i + 4, 4);
                    if (w == (float)g_winW && h == (float)g_winH) {
                        if (!dimOff) dimOff = i;
                        ++dimCount;
                    }
                }
            }
        }
        
        
        
        std::vector<uintptr_t> mats;
        {
            char blk[0x2000];
            if (ReadBytes(ve, blk, sizeof(blk))) {
                for (uintptr_t i = 0; i + 64 <= sizeof(blk); i += 4) {
                    float m[16];
                    memcpy(m, blk + i, 64);
                    if (MatFinite4(m) && MatOrtho4(m)) mats.push_back(i);
                }
            }
        }
        uintptr_t matOff = 0;
        for (uintptr_t c : mats) {
            float m[16];
            if (!ReadBytes(ve + c, (char*)m, 64)) continue;
            if (fabsf(m[3]) + fabsf(m[7]) + fabsf(m[11]) > 0.1f) {
                matOff = c;
                break;
            }
        }
        if (!matOff && !mats.empty()) matOff = mats[0];
        LogInfo("VE slot %#lx ve=%#lx fn=text dims%s(%d) mats=%zu",
                (unsigned long)slot, (unsigned long)ve,
                dimOff ? ("=+" + std::to_string(dimOff)).c_str() : "=(none)",
                dimCount, mats.size());
        if (dimOff && matOff) {
            Emit("Pointer", "VisualEngine", slot, "unsigned __int64");
            Emit("Dimensions", "VisualEngine", dimOff, "Vector2");
            Emit("ViewMatrix", "VisualEngine", matOff, "Matrix4x4");
            LogOk("VisualEngine static OK (ve=%#lx slot=%#lx dims+0x%lx mat+0x%lx)",
                  (unsigned long)ve, (unsigned long)slot,
                  (unsigned long)dimOff, (unsigned long)matOff);
            return;
        }
    }
    LogBad("STATIC IMPORT FAILED: VisualEngine (no validated slot)");
}

bool Static() {
    uintptr_t rx = ActiveRx();
    if (!rx) {
        LogBad("STATIC FAILED: no rx");
        return false;
    }
    g_rxActive = rx;
    LogInfo("active rx=%#lx", rx);
    std::vector<char> file;
    uintptr_t tsOff = 0;
    if (ReadFileBytes(file)) tsOff = DeriveSchedulerPointer(rx, file);
    if (tsOff)
        LogInfo("derived TaskScheduler::Pointer=0x%lx", (unsigned long)tsOff);
    uintptr_t sched = 0, fpsOff = 0;
    if (tsOff) {
        sched = Read<uintptr_t>(rx + tsOff);
        if (!(IsValid(sched) && FpsValid(sched, fpsOff))) {
            sched = 0;
            tsOff = 0;
        }
    }
    LogInfo("TaskScheduler=%#lx", sched);
    if (!sched) {
        LogBad("STATIC FAILED: derivation found nothing usable");
        return false;
    }
    LogInfo("MaxFPS double @+%#lx (1/%f)", (unsigned long)fpsOff, Read<double>(sched + fpsOff));
    Emit("Pointer", "TaskScheduler", tsOff, "unsigned __int64");
    Emit("MaxFPS", "TaskScheduler", fpsOff, "double");
    
    
    
    {
        uintptr_t nc = 0, noff = 0;
        if (DiscoverName(nc, noff)) {
            g_nameContainer = nc;
            g_nameOff = noff;
        }
        DiscoverChildren();
        LogInfo("layout: NC=0x%lx N=0x%lx CS=0x%lx stride=%lu",
                (unsigned long)g_nameContainer, (unsigned long)g_nameOff,
                (unsigned long)g_childrenStart,
                (unsigned long)g_childStride);
    }
    
    auto jobNameAt = [&](uintptr_t job, uintptr_t off) {
        char b[40];
        if (!ReadBytes(job + off, b, sizeof(b))) return std::string();
        auto dec = [&](int skip) -> std::string {
            int n = 0;
            while (n < 36 && b[skip + n] != 0) {
                if (b[skip + n] < 32 || b[skip + n] > 126) break;
                ++n;
            }
            if (n >= 3 && b[skip + n] == 0) return std::string(b + skip, n);
            return std::string();
        };
        std::string s1 = dec(1), s0 = dec(0);
        auto alpha0 = [](const std::string& s) {
            return !s.empty() && isalnum((unsigned char)s[0]);
        };
        if (alpha0(s1)) return s1;
        if (alpha0(s0)) return s0;
        if (!s1.empty()) return s1;
        return s0;
    };
    uintptr_t js = 0, je = 0, oStart = 0, oEnd = 0;
    const uintptr_t stride = 0x10;
    uintptr_t nameOff = 0;
    std::vector<uintptr_t> renderJobs;
    
    auto huntRender = [&](uintptr_t ljs, uintptr_t lje,
                          std::vector<uintptr_t>& out) {
        if (!(ljs && lje && lje > ljs)) return (uintptr_t)0;
        size_t n = (lje - ljs) / stride;
        if (n == 0 || n > 400) return (uintptr_t)0;
        for (uintptr_t r : {0x18u, 0x10u, 0x20u, 0x28u, 0x30u}) {
            out.clear();
            for (size_t k = 0; k < n && k < 300; ++k) {
                uintptr_t job = Read<uintptr_t>(ljs + k * stride);
                if (!IsValid(job)) continue;
                if (jobNameAt(job, r) == "RenderJob") out.push_back(job);
            }
            if (!out.empty()) return r;
        }
        return (uintptr_t)0;
    };
    
    
    {
        struct PF {
            uintptr_t off, val;
        };
        std::vector<PF> pfs;
        for (uintptr_t i = 0x40; i <= 0x300; i += 8) {
            uintptr_t v = Read<uintptr_t>(sched + i);
            if (IsValid(v)) pfs.push_back({i, v});
        }
        for (auto& a : pfs) {
            for (auto& b : pfs) {
                if (!(b.val > a.val && b.val - a.val <= 0x2000)) continue;
                if (b.off <= a.off) continue;
                size_t n = (b.val - a.val) / stride;
                if (n < 16 || n > 400) continue;
                std::vector<uintptr_t> found;
                uintptr_t r = huntRender(a.val, b.val, found);
                if (!found.empty()) {
                    js = a.val;
                    je = b.val;
                    oStart = a.off;
                    oEnd = b.off;
                    nameOff = r;
                    renderJobs = found;
                    LogInfo("job list discovered (+0x%lx/+0x%lx, " "RenderJob name@+0x%lx)", (unsigned long)oStart, (unsigned long)oEnd, (unsigned long)nameOff);
                    goto foundlist;
                }
            }
        }
    }
foundlist:
    if (!js) {  
        LogInfo("FALLBACK job list 0xd0/0xd8");
        uintptr_t tjs = Read<uintptr_t>(sched + 0xd0);
        uintptr_t tje = Read<uintptr_t>(sched + 0xd8);
        std::vector<uintptr_t> found;
        uintptr_t r = huntRender(tjs, tje, found);
        if (!found.empty()) {
            js = tjs;
            je = tje;
            oStart = 0xd0;
            oEnd = 0xd8;
            nameOff = r;
            renderJobs = found;
        }
    }
    if (!js) {
        LogBad("STATIC FAILED: no job list");
        return false;
    }
    Emit("JobStart", "TaskScheduler", oStart, "unsigned __int64");
    Emit("JobEnd", "TaskScheduler", oEnd, "unsigned __int64");
    size_t njob = (je - js) / stride;
    LogInfo("jobs: %zu", njob);
    if (renderJobs.empty()) {
        LogBad("STATIC FAILED: no RenderJob");
        return false;
    }
    Emit("JobName", "TaskScheduler", nameOff, "string");
    LogInfo("RenderJobs: %zu (name@+0x%lx)", renderJobs.size(), (unsigned long)nameOff);
    
    
    
    auto looksDataModel = [&](uintptr_t real) {
        uintptr_t holder = Read<uintptr_t>(real + g_childrenStart);
        if (!IsValid(holder)) return false;
        uintptr_t st = Read<uintptr_t>(holder);
        uintptr_t en = Read<uintptr_t>(holder + 8);
        if (!IsValid(st) || en < st) return false;
        size_t nk = (en - st) / 16;
        return nk >= 20 && nk <= 1000;
    };
    auto placeAt = [&](uintptr_t real) {
        char d[0x1000];
        if (!ReadBytes(real, d, sizeof(d))) return false;
        for (size_t i = 0; i + 8 <= sizeof(d); i += 8) {
            uint64_t v;
            memcpy(&v, d + i, 8);
            if (v == g_placeId) return true;
        }
        return false;
    };
    struct Cand {
        uintptr_t o1, o2, real;
    };
    auto nearest = [](uintptr_t a, uintptr_t b) {
        uintptr_t da = a > 0x30 ? a - 0x30 : 0x30 - a;
        uintptr_t db = b > 0x1d0 ? b - 0x1d0 : 0x1d0 - b;
        return da + db;
    };
    auto dmAlive = [&](uintptr_t d) {
        return d && FindFirstChild(d, "Workspace") &&
               FindFirstChild(d, "Players");
    };
    
    
    
    
    
    uintptr_t fbO1 = 0, fbO2 = 0, fbReal = 0;  
    for (uintptr_t job : renderJobs) {
        uintptr_t bestO1 = 0, bestO2 = 0, bestReal = 0;
        int bestScore = -1;
        for (uintptr_t o1 = 0x20; o1 < 0x100; o1 += 8) {
            uintptr_t fake = Read<uintptr_t>(job + o1);
            if (!IsValid(fake)) continue;
            char fb[0x300];
            if (!ReadBytes(fake, fb, sizeof(fb))) continue;
            for (uintptr_t o2 = 0x100; o2 < 0x300; o2 += 8) {
                uint64_t real;
                memcpy(&real, fb + o2, 8);
                if (!IsValid(real)) continue;
                if (GetName(real) != "Ugc") continue;
                if (!looksDataModel(real)) continue;
                int score = (placeAt(real) ? 4 : 0) +
                            (dmAlive(real) ? 2 : 0);
                if (score <= bestScore) continue;
                bestScore = score;
                bestO1 = o1;
                bestO2 = o2;
                bestReal = real;
            }
        }
        if (bestReal && bestScore >= 4) {
            Emit("FakeDataModel", "RenderJob", bestO1, "unsigned __int64");
            Emit("RealDataModel", "RenderJob", bestO2, "unsigned __int64");
            Emit("RealDataModel", "FakeDataModel", bestO2,
                 "unsigned __int64");
            g_dataModel = bestReal;
            ValidateImports(rx);
            LogOk("DataModel=%#lx (RenderJob %#lx, UGC-NAME o1=%#lx o2=%#lx)", bestReal, job, (unsigned long)bestO1, (unsigned long)bestO2);
            return true;
        }
        
        if (bestReal && bestScore >= 2 && !fbReal) {
            fbO1 = bestO1;
            fbO2 = bestO2;
            fbReal = bestReal;
        }
    }
    LogInfo("ugc-name missed, nested discovery...");
    for (uintptr_t job : renderJobs) {
        char jb[0x300];
        if (!ReadBytes(job, jb, sizeof(jb))) continue;
        std::vector<Cand> cands;
        for (uintptr_t o1 = 0x20; o1 < 0x100; o1 += 8) {
            uint64_t fake;
            memcpy(&fake, jb + o1, 8);
            if (!IsValid(fake)) continue;
            char fb[0x300];
            if (!ReadBytes(fake, fb, sizeof(fb))) continue;
            for (uintptr_t o2 = 0x100; o2 < 0x300; o2 += 8) {
                uint64_t real;
                memcpy(&real, fb + o2, 8);
                if (!IsValid(real)) continue;
                bool dup = false;
                for (auto& c : cands)
                    if (c.real == real) {
                        dup = true;
                        break;
                    }
                if (!dup) cands.push_back({o1, o2, real});
            }
        }
        
        Cand* best = nullptr;
        for (auto& c : cands) {
            if (!placeAt(c.real) || !looksDataModel(c.real)) continue;
            if (!best || nearest(c.o1, c.o2) < nearest(best->o1, best->o2))
                best = &c;
        }
        if (best) {
            Emit("FakeDataModel", "RenderJob", best->o1, "unsigned __int64");
            Emit("RealDataModel", "RenderJob", best->o2, "unsigned __int64");
            
            
            
            
            
            Emit("RealDataModel", "FakeDataModel", best->o2,
                 "unsigned __int64");
            g_dataModel = best->real;
            ValidateImports(rx);
            LogOk("DataModel=%#lx (RenderJob %#lx, discovered o1=%#lx o2=%#lx)", best->real, job, (unsigned long)best->o1, (unsigned long)best->o2);
            return true;
        }
    }
    LogInfo("nested missed, FALLBACK hardcoded 0x30/0x1d0...");
    if (fbReal) {
        
        
        Emit("FakeDataModel", "RenderJob", fbO1, "unsigned __int64");
        Emit("RealDataModel", "RenderJob", fbO2, "unsigned __int64");
        Emit("RealDataModel", "FakeDataModel", fbO2, "unsigned __int64");
        g_dataModel = fbReal;
        ValidateImports(rx);
        LogOk("DataModel=%#lx (TELEPORT-TIER o1=%#lx o2=%#lx, place unknown)",
              fbReal, (unsigned long)fbO1, (unsigned long)fbO2);
        return true;
    }
    for (uintptr_t job : renderJobs) {
        uintptr_t fake = Read<uintptr_t>(job + 0x30);
        if (!IsValid(fake)) continue;
        uintptr_t real = Read<uintptr_t>(fake + 0x1d0);
        if (!IsValid(real) || !placeAt(real) || !looksDataModel(real))
            continue;
        Emit("FakeDataModel", "RenderJob", 0x30, "unsigned __int64");
        Emit("RealDataModel", "RenderJob", 0x1d0, "unsigned __int64");
        Emit("RealDataModel", "FakeDataModel", 0x1d0, "unsigned __int64");
        g_dataModel = real;
        ValidateImports(rx);
        LogOk("DataModel=%#lx (RenderJob %#lx, FALLBACK)", real, job);
        return true;
    }
    LogBad("STATIC FAILED: no RenderJob->DataModel");
    return false;
}
}  
