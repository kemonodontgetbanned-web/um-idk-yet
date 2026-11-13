#include "dump.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "globals.h"
#include "log.h"
#include "mem.h"
#include "roblox.h"
#include "scanner.h"

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







static bool ValidPrim(uintptr_t prim) {
    float cf[12], dup[12], sz[3];
    if (!ReadBytes(prim + 0xB0, (char*)cf, 48)) return false;
    if (!ReadBytes(prim + 0xF8, (char*)dup, 48)) return false;
    if (!ReadBytes(prim + 0x1BC, (char*)sz, 12)) return false;
    for (int i = 0; i < 12; ++i)  
        if (fabsf(cf[i] - dup[i]) > 0.001f) return false;  
                                                          
    float* pos = cf + 9;
    float np = sqrtf(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]);
    if (!std::isfinite(np) || np < 0.5 || np > 1e6) return false;
    for (int r = 0; r < 3; ++r) {
        float n = sqrtf(cf[r * 3] * cf[r * 3] + cf[r * 3 + 1] * cf[r * 3 + 1] +
                        cf[r * 3 + 2] * cf[r * 3 + 2]);
        if (!std::isfinite(n) || fabsf(n - 1) > 0.05) return false;
    }
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(sz[i]) || sz[i] <= 0 || sz[i] > 10000) return false;
    return true;
}

bool Parts() {
    
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    uintptr_t players = FindFirstChild(g_dataModel, "Players");
    uintptr_t head = 0, seat = 0;
    if (players) {
        for (uintptr_t c : GetChildren(players)) {
            if (ClassOf(c) != "Player") continue;
            if (Read<uint64_t>(c + 0xD0) != g_userId) continue;
            uintptr_t model = Read<uintptr_t>(c + 0x250);
            if (model) head = FindFirstChild(model, "Head");
            break;
        }
    }
    if (ws) seat = FindFirstChild(ws, "Seat");
    
    
    uintptr_t primOff = 0;
    int nvalid = 0;
    for (uintptr_t poff : {0x168u, 0x178u}) {
        nvalid = 0;
        for (uintptr_t part : {head, seat}) {
            if (!part) continue;
            uintptr_t p = Read<uintptr_t>(part + poff);
            if (p && ValidPrim(p)) {
                ++nvalid;
                LogInfo("Primitive OK via %s (%#lx -> %#lx) [slot %#lx]",
                        GetName(part).c_str(), (unsigned long)part,
                        (unsigned long)p, (unsigned long)poff);
            }
        }
        if (nvalid) {
            primOff = poff;
            break;
        }
    }
    if (primOff) {
        Emit("Primitive", "BasePart", primOff, "unsigned __int64");
        Emit("Position", "Primitive", 0xD4, "Vector3");
        Emit("CFrame", "Primitive", 0xB0, "CFrame");
        
        Emit("Rotation", "Primitive", 0xB0, "Matrix3x3");
        Emit("Size", "Primitive", 0x1BC, "Vector3");
        
        
        
        Emit("AssemblyLinearVelocity", "Primitive", 0xE0, "Vector3");
    } else {
        LogBad("MISS   %-28s %-14s (no self-consistent prim)", "Primitive",
               "BasePart");
    }
    
    uintptr_t teams = FindFirstChild(g_dataModel, "Teams");
    bool teamOk = false;
    if (teams) {
        for (uintptr_t t : GetChildren(teams)) {
            if (ClassOf(t) != "Team") continue;
            uintptr_t at = 0;
            for (uintptr_t coff : {0xA8u, 0xB8u}) {
                uint32_t v = Read<uint32_t>(t + coff);
                LogInfo("Team %s color=%u @+%#lx", GetName(t).c_str(), v,
                        (unsigned long)coff);
                if (!at && (int)v == g_teamColor) at = coff;
            }
            if (at) {
                Emit("TeamColor", "Team", at, "unsigned int");
                teamOk = true;
                break;
            }
        }
    }
    if (!teamOk)
        LogBad("MISS   %-28s %-14s (want %d)", "TeamColor", "Team",
               g_teamColor);
    
    
    
    
    
    
    auto primOf = [&](uintptr_t part) -> uintptr_t {
        return (part && primOff) ? Read<uintptr_t>(part + primOff) : 0;
    };
    uintptr_t posPart = ws ? FindFirstChild(ws, "Position") : 0;
    uintptr_t sizePart = ws ? FindFirstChild(ws, "Size") : 0;
    uintptr_t rotPart = ws ? FindFirstChild(ws, "Rotation") : 0;
    if (posPart) {
        
        
        
        
        uintptr_t posPrim = primOf(posPart);
        if (posPrim && IsValid(posPrim))
            ScanU64(posPrim, posPart, "Owner", "Primitive");
    }
    {
        
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        uintptr_t ps[3] = {posPart, sizePart, rotPart};
        int want[3] = {1, 0, 2};
        for (int k = 0; k < 3; ++k)
            if (ps[k]) {
                is.push_back(ps[k]);
                vs.push_back(want[k]);
            }
        if (!is.empty()) ScanIntM(is, vs, "Shape", "BasePart");
    }
    {
        
        
        
        
        uintptr_t pp[3] = {primOf(posPart), primOf(sizePart),
                             primOf(rotPart)};
        if (pp[0] && pp[1] && pp[2]) {
            bool done = false;
            for (uintptr_t i = 0; i < 0x1000 && !done; i += 1) {
                char b[3] = {Read<char>(pp[0] + i), Read<char>(pp[1] + i),
                             Read<char>(pp[2] + i)};
                
                if (((b[0] & 8) != 0) && !((b[1] & 8) != 0) &&
                    ((b[2] & 8) != 0) && ((b[0] & 2) != 0) &&
                    ((b[1] & 2) != 0) && ((b[2] & 2) != 0)) {
                    
                    char c[3];
                    if (!ReadBytes(pp[0] + i, c, 1) ||
                        !ReadBytes(pp[1] + i, c + 1, 1) ||
                        !ReadBytes(pp[2] + i, c + 2, 1))
                        continue;
                    Emit("Flags", "Primitive", i, "BYTE");
                    done = true;
                }
            }
            if (!done)
                LogBad("MISS   %-28s %-14s (flag-bit differential)", "Flags",
                       "Primitive");
        }
    }
    {
        
        uintptr_t vel = ws ? FindFirstChild(ws, "Velocity") : 0;
        uintptr_t velPrim = primOf(vel);
        if (velPrim && IsValid(velPrim))
            ScanVec3(velPrim, 67.f, 67.69f, 6.f, "AssemblyAngularVelocity",
                     "Primitive");
    }
    {
        
        uintptr_t tp[4] = {0};
        const char* tn[4] = {"ThePart", "ThePart2", "ThePart3", "ThePart4"};
        for (int k = 0; k < 4; ++k)
            tp[k] = ws ? FindFirstChild(ws, tn[k]) : 0;
        if (tp[0]) ScanFlt(tp[0], 0.832f, "Reflectance", "BasePart");
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        auto pickB = [&](std::vector<int> w) {
            is.clear();
            vs.clear();
            for (int k = 0; k < 4; ++k)
                if (tp[k]) {
                    is.push_back(tp[k]);
                    vs.push_back(w[k]);
                }
        };
        pickB({1, 0, 1, 0});
        if (!is.empty()) ScanBoolM(is, vs, "CastShadow", "BasePart");
        pickB({0, 1, 0, 1});
        if (!is.empty()) ScanBoolM(is, vs, "Locked", "BasePart");
        pickB({1, 0, 1, 1});
        if (!is.empty()) ScanBoolM(is, vs, "Massless", "BasePart");
    }
    {
        
        uintptr_t model = ws ? FindFirstChild(ws, "Model") : 0;
        if (model) {
            auto ck = GetChildren(model);
            if (!ck.empty()) ScanU64(model, ck[0], "PrimaryPart", "Model");
            ScanFlt(model, 1.622f, "Scale", "Model");
        }
    }
    
    
    
    LogInfo("SKIP PrimitiveFlags/RenderView (foreign layout dead)");
    return true;
}
}  
