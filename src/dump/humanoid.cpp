#include "dump.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>

#include "globals.h"
#include "log.h"
#include "mem.h"
#include "roblox.h"
#include "rtti.h"
#include "scanner.h"

namespace Dump {









struct PropWant {
    const char* name;  
    const char* bin;   
    int size;          
    int known;         
};

bool Humanoid() {
    
    
    std::vector<char> file;
    {
        FILE* f = fopen(g_dumpedPath.c_str(), "rb");
        if (!f) {
            LogBad("HumanoidProps FAILED: no reference binary");
            return false;
        }
        char b[1 << 20];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) > 0)
            file.insert(file.end(), b, b + n);
        fclose(f);
    }
    auto u8 = [&](size_t i) -> unsigned char {
        return static_cast<unsigned char>(file[i]);
    };
    
    auto findStrings = [&](const char* name) {
        std::vector<uint64_t> out;
        size_t nl = strlen(name);
        size_t off = 0;
        while (off + nl + 1 <= file.size()) {
            const void* p =
                memmem(file.data() + off, file.size() - off, name, nl);
            if (!p) break;
            size_t s = (const char*)p - file.data();
            off = s + 1;
            if (file[s + nl] != 0) continue;
            if (s > 0) {
                unsigned char pre = file[s - 1];
                if ((pre >= 'A' && pre <= 'Z') ||
                    (pre >= 'a' && pre <= 'z') ||
                    (pre >= '0' && pre <= '9') || pre == '_')
                    continue;
            }
            out.push_back(s);
            if (out.size() >= 8) break;
        }
        return out;
    };
    
    const PropWant wants[] = {
        {"Walkspeed", "WalkSpeed", 4, 0x1B0},
        {"Health", NULL, 4, 0x170},         {"MaxHealth", NULL, 4, 0x188},
        {"JumpPower", NULL, 4, 0x184},      {"JumpHeight", NULL, 4, 0x180},
        {"MaxSlopeAngle", NULL, 4, 0x18C},  {"HipHeight", NULL, 4, 0x174},
        {"UseJumpPower", NULL, 1, 0x1C0},
        {"AutoJumpEnabled", NULL, 1, -1},
        {"AutoRotate", NULL, 1, -1},
        {"AutomaticScalingEnabled", NULL, 1, -1},
        {"BreakJointsOnDeath", NULL, 1, -1},
        {"EvaluateStateMachine", NULL, 1, -1},
        {"RequiresNeck", NULL, 1, -1},      {"Sit", NULL, 1, -1},
        {"Jump", NULL, 1, -1},               {"PlatformStand", NULL, 1, -1},
        {"DisplayDistanceType", NULL, 1, -1},
        {"HealthDisplayType", NULL, 1, -1},
        {"NameOcclusion", NULL, 1, -1},
        {"RigType", NULL, 1, -1},           {"CameraOffset", NULL, 12, -1},
        {"TargetPoint", NULL, 12, -1},      {"WalkToPoint", NULL, 12, -1},
        {"HealthDisplayDistance", NULL, 4, -1},
        {"NameDisplayDistance", NULL, 4, -1},
        {"SeatPart", NULL, 8, -1},
    };
    
    struct SW {
        uint64_t va;
        int wi;
    };
    std::vector<SW> strvas;
    for (size_t wi = 0; wi < sizeof(wants) / sizeof(wants[0]); ++wi) {
        const char* s = wants[wi].bin ? wants[wi].bin : wants[wi].name;
        for (uint64_t v : findStrings(s)) strvas.push_back({v, (int)wi});
    }
    
    
    
    struct Site {
        int wi;
        size_t pos;
    };
    std::vector<Site> sites;
    for (size_t i = 0; i + 7 < file.size(); ++i) {
        uint64_t t = 0;
        if (u8(i) >= 0x48 && u8(i) <= 0x4F &&
            (u8(i + 1) == 0x8D || u8(i + 1) == 0x8B ||
             u8(i + 1) == 0x89) &&
            ((u8(i + 2) & 0xC7) == 0x05)) {
            int32_t d;
            memcpy(&d, &file[i + 3], 4);
            t = i + 7 + (int64_t)d;
        } else if ((u8(i) == 0x8D || u8(i) == 0x8B || u8(i) == 0x89) &&
                   ((u8(i + 1) & 0xC7) == 0x05)) {
            int32_t d;
            memcpy(&d, &file[i + 2], 4);
            t = i + 6 + (int64_t)d;
        } else {
            continue;
        }
        int wi = -1;
        for (auto& sw : strvas)
            if (sw.va == t) {
                wi = sw.wi;
                break;
            }
        if (wi < 0) continue;
        sites.push_back({wi, i});
        i += 6;
    }
    LogInfo("registration sites: %zu", sites.size());
    
    
    
    auto parseLoad = [&](const unsigned char* p, size_t j, int64_t& disp,
                         int& size) {
        bool w = false;
        if (p[j] >= 0x40 && p[j] <= 0x4F) {
            if (p[j] & 1) return false;  
            w = (p[j] & 8) != 0;
            j += 1;
        }
        int sz = 0;
        if (p[j] == 0xF3 && p[j + 1] == 0x0F &&
            (p[j + 2] == 0x10 || p[j + 2] == 0x11))
            sz = 4;
        else if (p[j] == 0x0F && (p[j + 1] == 0x10 || p[j + 1] == 0x11))
            sz = 16;
        else if (p[j] == 0x0F && p[j + 1] == 0xB6)
            sz = 1;
        else if (p[j] == 0x8A || p[j] == 0x88)
            sz = 1;
        else if (p[j] == 0x8B || p[j] == 0x89)
            sz = w ? 8 : 4;
        else
            return false;
        if (p[j] == 0xF3) j++;                            
        if (p[j] >= 0x40 && p[j] <= 0x4F) j++;             
        if (p[j] == 0x0F) j += 2;                         
        else
            j += 1;                                       
        unsigned char mr = p[j];
        if ((mr & 7) != 7) return false;                  
        int mod = (mr >> 6) & 3;
        if (mod == 0)
            disp = 0;
        else if (mod == 1)
            disp = (int8_t)p[j + 1];
        else if (mod == 2) {
            int32_t d;
            memcpy(&d, &p[j + 1], 4);
            disp = d;
        } else
            return false;  
        if (disp == 0 && sz == 8) return false;           
        size = sz;
        return true;
    };
    
    auto getterLoad = [&](uint64_t fnVA, int64_t& disp, int& size) {
        uintptr_t t0 = 0, tsz = 0;
        if (!TextFileRange(t0, tsz)) return false;
        if (fnVA < t0 || fnVA + 32 >= t0 + tsz) return false;
        const unsigned char* p = (const unsigned char*)&file[(size_t)fnVA];
        size_t j = 0;
        for (;;) {
            if (j + 4 <= 32 && p[j] == 0xF3 && p[j + 1] == 0x0F &&
                p[j + 2] == 0x1E && p[j + 3] == 0xFA) {
                j += 4;
                continue;
            }
            if (p[j] == 0x90) {
                j += 1;
                continue;
            }
            break;
        }
        for (size_t s = j; s + 10 <= 32; ++s)
            if (parseLoad(p, s, disp, size)) return true;
        return false;
    };
    uintptr_t t0 = 0, tsz = 0;
    TextFileRange(t0, tsz);
    int validated = 0, emitted = 0, corrected = 0;
    const size_t NW = sizeof(wants) / sizeof(wants[0]);
    
    
    int64_t interior = 0xA0;
    bool interiorCal = false;
    for (size_t wi = 0; wi < NW; ++wi) {
        const PropWant& w = wants[wi];
        struct Ev {
            int64_t off;
            int size;
            uint64_t fn;
        };
        std::vector<Ev> evs;
        int nsites = 0;
        for (auto& s : sites) {
            if (s.wi != (int)wi) continue;
            ++nsites;
            size_t lo = s.pos > 96 ? s.pos - 96 : 0;
            size_t hi = s.pos + 96;
            if (hi > file.size()) hi = file.size();
            for (size_t k = lo; k + 7 < hi; ++k) {
                uint64_t tk = 0;
                size_t ilen = 0;
                if (k + 7 < file.size() && u8(k) >= 0x48 && u8(k) <= 0x4F &&
                    u8(k + 1) == 0x8D && ((u8(k + 2) & 0xC7) == 0x05)) {
                    int32_t d;
                    memcpy(&d, &file[k + 3], 4);
                    tk = k + 7 + (int64_t)d;
                    ilen = 7;
                } else if (k + 6 < file.size() && u8(k) == 0x8D &&
                           ((u8(k + 1) & 0xC7) == 0x05)) {
                    int32_t d;
                    memcpy(&d, &file[k + 2], 4);
                    tk = k + 6 + (int64_t)d;
                    ilen = 6;
                } else {
                    continue;
                }
                if (tk < t0 || tk >= t0 + tsz) continue;
                bool dup = false;
                for (auto& e : evs)
                    if (e.fn == tk) dup = true;
                if (!dup) {
                    int64_t dp = 0;
                    int sz = 0;
                    
                    if (getterLoad(tk, dp, sz)) {
                        int64_t off = dp + 0xB0;  
                        if (off >= 0 && off < 0x1000)
                            evs.push_back({off, sz, tk});
                    }
                }
                k += ilen - 1;
            }
        }
        int wantSize = w.size == 12 ? 16 : w.size;
        std::vector<Ev> fit;
        for (auto& e : evs)
            if (e.size == wantSize) fit.push_back(e);
        uintptr_t have = 0;
        for (auto& e : g_emitted)
            if (e.cat == "Humanoid" && strcasecmp(e.name.c_str(), w.name) == 0)
                have = e.val;
        if (!interiorCal && have && !fit.empty()) {
            
            
            int64_t best = 0;
            int bestN = 0;
            for (auto& e : fit) {
                int64_t c = (int64_t)have - (e.off - 0xB0);
                int n = 0;
                for (auto& f : fit)
                    if ((int64_t)have - (f.off - 0xB0) == c) ++n;
                if (n > bestN) {
                    bestN = n;
                    best = c;
                }
            }
            if (best >= 0x40 && best <= 0x100) {
                if (best != interior)
                    LogOk("interior this recalibrated +0x%lx -> +0x%lx (live %s=%#lx)",
                          (unsigned long)interior, (unsigned long)best, w.name,
                          (unsigned long)have);
                else
                    LogOk("interior this +0x%lx confirmed live (%s=%#lx)",
                          (unsigned long)interior, w.name,
                          (unsigned long)have);
                interior = best;
                interiorCal = true;
            } else {
                LogBad("interior insane +0x%lx from %s (keeping +0x%lx)",
                       (unsigned long)best, w.name, (unsigned long)interior);
            }
        }
        if (!interiorCal && wi + 1 >= NW)
            LogBad("interior UNCALIBRATED (no live anchor): using default +0x%lx",
                   (unsigned long)interior);
        for (auto& e : fit) e.off += interior - 0xB0;
        
        
        
        bool hitHave = false, hitKnown = false;
        for (auto& e : fit) {
            if (have && e.off == (int64_t)have) hitHave = true;
            if (w.known >= 0 && e.off == (int64_t)w.known) hitKnown = true;
        }
        bool agree = true;
        for (auto& e : fit)
            if (e.off != fit[0].off) agree = false;
        const char* ty = w.size == 1    ? "bool"
                         : w.size == 12 ? "Vector3"
                         : w.size == 8  ? "unsigned __int64"
                                        : "float";
        if (w.known >= 0) {
            if (hitKnown) {
                ++validated;
                if (!have) {
                    
                    Emit(w.name, "Humanoid", (uintptr_t)w.known, ty);
                    LogOk("%-22s off=%#x nev=%zu (getter gap-fill)", w.name,
                          w.known, fit.size());
                    ++emitted;
                } else if ((int64_t)have == (int64_t)w.known) {
                    LogOk("%-22s off=%#x nev=%zu OK", w.name, w.known,
                          fit.size());
                } else {
                    
                    
                    
                    if (!Correct(w.name, "Humanoid", (uintptr_t)w.known))
                        Emit(w.name, "Humanoid", (uintptr_t)w.known, ty);
                    for (auto& e : fit)
                        LogInfo("    ev fn=%#lx off=%#lx sz=%d",
                                (unsigned long)e.fn, (unsigned long)e.off,
                                e.size);
                    ++corrected;
                }
            } else {
                LogBad("%-22s NO getter match for known %#x (nev=%zu)",
                       w.name, w.known, fit.size());
                for (auto& e : fit)
                    LogInfo("    ev fn=%#lx off=%#lx sz=%d",
                            (unsigned long)e.fn, (unsigned long)e.off, e.size);
            }
            continue;
        }
        if (have) {
            if (hitHave)
                LogOk("%-22s agrees with scan (%#lx nev=%zu)", w.name,
                      (unsigned long)have, fit.size());
            else if (agree && fit.size() >= 2) {
                
                
                
                if (!Correct(w.name, "Humanoid", (uintptr_t)fit[0].off))
                    Emit(w.name, "Humanoid", (uintptr_t)fit[0].off, ty);
                for (auto& e : fit)
                    LogInfo("    ev fn=%#lx off=%#lx sz=%d",
                            (unsigned long)e.fn, (unsigned long)e.off, e.size);
                ++corrected;
            } else {
                LogBad("%-22s scan=%#lx getter-nev=%zu (scan kept, UNSURE)",
                       w.name, (unsigned long)have, fit.size());
                for (auto& e : fit)
                    LogInfo("    ev fn=%#lx off=%#lx sz=%d",
                            (unsigned long)e.fn, (unsigned long)e.off, e.size);
            }
            continue;
        }
        if (fit.empty()) {
            
            
            
            LogInfo("%-22s no getter evidence (sites=%d, shared getter?)",
                    w.name, nsites);
            continue;
        }
        if (agree) {
            Emit(w.name, "Humanoid", (uintptr_t)fit[0].off, ty);
            LogOk("%-22s off=%#lx nev=%zu (getter)", w.name,
                  (unsigned long)fit[0].off, fit.size());
            ++emitted;
        } else {
            LogBad("%-22s getter evidences disagree (%zu)", w.name,
                   fit.size());
            for (auto& e : fit)
                LogInfo("    ev fn=%#lx off=%#lx sz=%d", (unsigned long)e.fn,
                        (unsigned long)e.off, e.size);
        }
    }
    
    
    
    
    
    
    Emit("TargetPoint", "Humanoid", 0x12C, "Vector3");
    LogInfo("imported TargetPoint=0x12c (uniform-shift inference; live scan wins when rig state allows)");
    LogInfo("getter validated=%d emitted=%d corrected=%d", validated, emitted,
              corrected);
    return true;
}
}  
