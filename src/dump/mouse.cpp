#include "dump.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#include "globals.h"
#include "mem.h"
#include "roblox.h"
#include "rtti.h"
#include "scanner.h"
#include "log.h"

namespace Dump {

static std::string ClassOf(uintptr_t inst) {
    uintptr_t d = Read<uintptr_t>(inst + 0x18);
    if (!IsValid(d)) return "";
    uintptr_t np = Read<uintptr_t>(d + 8);
    if (!IsValid(np)) return "";
    char b[40];
    if (!ReadBytes(np, b, sizeof(b))) return "";
    for (int sk = 0; sk < 3; ++sk) {
        int n = 0;
        while (n < 36 && b[sk + n] != 0) {
            if (b[sk + n] < 32 || b[sk + n] > 126) break;
            ++n;
        }
        if (n > 0 && b[sk + n] == 0) return std::string(b + sk, n);
    }
    return "";
}

bool Mouse() {
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    uintptr_t ms = FindFirstChild(g_dataModel, "MouseService");
    if (!ws || !ms) {
        LogBad("Mouse FAILED: ws=%#lx ms=%#lx", ws, ms);
        return false;
    }
    
    
    
    
    
    
    
    
    
    
    
    uintptr_t mpf = FindFirstChild(ws, "MousePosition");
    bool derived = false;
    if (mpf) {
        auto ck = GetChildren(mpf);
        if (!ck.empty()) {
            float cx = 0, cy = 0;
            if (sscanf(GetName(ck[0]).c_str(), "%f,%f", &cx, &cy) == 2 &&
                std::isfinite(cx) && std::isfinite(cy)) {
                
                
                
                
                
                
                
                struct Cand {
                    uintptr_t i, j;
                    float x0, y0;
                };
                std::vector<Cand> cands;
                for (uintptr_t i = 0xE0; i < 0x250; i += 8) {
                    uintptr_t io = Read<uintptr_t>(ms + i);
                    if (!IsValid(io) || ClassOf(io) != "InputObject")
                        continue;
                    for (uintptr_t j = 0xB0; j < 0x100; j += 4) {
                        float v[2];
                        if (!ReadBytes(io + j, (char*)v, 8)) continue;
                        if (!std::isfinite(v[0]) || !std::isfinite(v[1]))
                            continue;
                        
                        
                        if (fabsf(v[0]) + fabsf(v[1]) < 1.0f) continue;
                        if (fabsf(v[0] - cx) <= 2.0f &&
                            fabsf(v[1] - cy) <= 80.0f) {
                            
                            if (v[0] < 0 || v[1] < 0 ||
                                v[0] > (float)g_winW ||
                                v[1] > (float)g_winH)
                                continue;
                            cands.push_back({i, j, v[0], v[1]});
                        }
                    }
                }
                uintptr_t bi = 0, bj = 0;
                const char* how = "first-match";
                auto useCand = [&](Cand c, const char* h) {
                    bi = c.i;
                    bj = c.j;
                    how = h;
                };
                if (!cands.empty()) {
                    useCand(cands[0], "first-match");
                    for (auto& c : cands)
                        if (fabsf(c.x0 - cx) <= 2.0f &&
                            fabsf(c.y0 - cy) <= 2.0f) {
                            useCand(c, "exact");
                            break;
                        }
                    
                    usleep(300000);
                    auto ck2 = GetChildren(mpf);
                    float dx = 0, dy = 0;
                    if (!ck2.empty() &&
                        sscanf(GetName(ck2[0]).c_str(), "%f,%f", &dx, &dy) ==
                            2 &&
                        std::isfinite(dx) && std::isfinite(dy) &&
                        (fabsf(dx - cx) > 5.0f ||
                         fabsf(dy - cy) > 5.0f)) {
                        bool tracked = false;
                        for (auto& c : cands) {
                            uintptr_t io = Read<uintptr_t>(ms + c.i);
                            if (!IsValid(io)) continue;
                            float v[2];
                            if (!ReadBytes(io + c.j, (char*)v, 8)) continue;
                            if (fabsf((v[0] - c.x0) - (dx - cx)) <= 4.0f &&
                                fabsf((v[1] - c.y0) - (dy - cy)) <= 4.0f) {
                                useCand(c, "delta-tracked");
                                tracked = true;
                                break;
                            }
                        }
                        if (!tracked) how = "first-match (stale?)";
                    } else if (!strcmp(how, "first-match")) {
                        how = "first-match (motionless)";
                    }
                    uintptr_t io = Read<uintptr_t>(ms + bi);
                    float v[2] = {0, 0};
                    ReadBytes(io + bj, (char*)v, 8);
                    Emit("InputObject", "MouseService", bi,
                         "unsigned __int64");
                    Emit("MousePosition", "InputObject", bj, "Vector2");
                    LogInfo("MousePosition=(%.1f,%.1f) want=(%.1f,%.1f) "
                            "[%s, %zu cands]",
                            v[0], v[1], (double)cx, (double)cy, how,
                            cands.size());
                    
                    
                    uintptr_t io2 = Read<uintptr_t>(ms + bi + 0x10);
                    if (IsValid(io2) && ClassOf(io2) == "InputObject")
                        Emit("InputObject2", "MouseService", bi + 0x10,
                             "unsigned __int64");
                    else
                        LogBad("MISS   %-28s %-14s (slot +0x10=%#lx %s)",
                               "InputObject2", "MouseService",
                               (unsigned long)io2, ClassOf(io2).c_str());
                    derived = true;
                }
            }
        }
    }
    if (!derived)
        LogBad("MISS   %-28s %-14s (MousePosition-folder nested)",
               "InputObject", "MouseService");
    return true;
}
}  
