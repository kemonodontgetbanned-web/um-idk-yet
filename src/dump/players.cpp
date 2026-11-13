#include "dump.h"

#include <algorithm>
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
bool Players() {
    uintptr_t players = FindFirstChild(g_dataModel, "Players");
    LogInfo("Players=%#lx", players);
    if (!players) return false;
    auto kids = GetChildren(players);
    LogInfo("Players children: %zu", kids.size());
    uintptr_t local = 0;
    for (uintptr_t c : kids) {  
        for (uintptr_t i = 0; i < 0x1000; i += 8)
            if (Read<uint64_t>(c + i) == g_userId) {
                local = c;
                break;
            }
        if (local) break;
    }
    if (!local && !kids.empty()) local = kids[0];
    LogInfo("LocalPlayer=%#lx name=%s", local, GetName(local).c_str());
    if (!local) return false;
    ScanU64(players, local, "LocalPlayer", "Player");
    ScanU64(local, g_userId, "UserId", "Player");
    ScanStr(local, g_displayName, "DisplayName", "Player", 0x200);
    ScanStr(local, g_localeId, "LocaleId", "Player", 0x200);
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    uintptr_t ch = FindFirstChild(ws, GetName(local));
    LogInfo("Character=%#lx", ch);
    if (!ch) {
        printf("workspace children:\n");
        for (uintptr_t c : GetChildren(ws))
            printf("   %#lx %s\n", c, GetName(c).c_str());
        return false;
    }
    ScanU64(local, ch, "ModelInstance", "Player");
    
    
    
    ScanI32(local, g_teamColor, "TeamColor", "Player", 0x300, 0x400);
    uintptr_t hum = FindFirstChild(ch, "Humanoid");
    LogOk("Humanoid=%#lx", hum);
    if (!hum) {
        printf("character children:\n");
        for (uintptr_t c : GetChildren(ch))
            printf("   %#lx %s\n", c, GetName(c).c_str());
        return false;
    }
    ScanFlt(hum, g_fHealth, "Health", "Humanoid");
    ScanFlt(hum, g_fMaxHealth, "MaxHealth", "Humanoid");
    uintptr_t wsoff = ScanFlt(hum, g_fWalk, "Walkspeed", "Humanoid");
    uintptr_t checkOff = 0;
    if (wsoff)
        checkOff =
            ScanFlt(hum, g_fWalk, "WalkspeedCheck", "Humanoid",
                    (wsoff + 4) & ~3u);
    ScanFlt(hum, g_fJumpPower, "JumpPower", "Humanoid");
    ScanFlt(hum, g_fJumpHeight, "JumpHeight", "Humanoid");
    ScanFlt(hum, g_fHip, "HipHeight", "Humanoid", 0, 0x100);
    ScanFlt(hum, g_fSlope, "MaxSlopeAngle", "Humanoid");
    if (wsoff && checkOff) {
        
        
        
        if (wsoff != 0x1B0 || checkOff != 0x38C)
            LogBad("DRIFT? walkspeed@%#lx check@%#lx (IDA: 0x1b0/0x38c)", (unsigned long)wsoff, (unsigned long)checkOff);
        float wspeed = Read<float>(hum + wsoff);
        float wchk = Read<float>(hum + checkOff);
        LogOk("WalkSpeed[%#lx]=%f WalkSpeedCheck[%#lx]=%f", hum + wsoff, wspeed, hum + checkOff, wchk);
    }
    
    
    uintptr_t rHumanoid = FindFirstChild(ws, "Humanoid");
    uintptr_t rig = FindFirstChild(ws, "Rig");
    uintptr_t rigHumanoid = rig ? FindFirstChild(rig, "Humanoid") : 0;
    uintptr_t rH[8] = {0};
    rH[1] = rHumanoid;
    const char* rnames[8] = {"", "", "Humanoid2", "Humanoid3", "Humanoid4",
                             "Humanoid5", "Humanoid6", "Humanoid7"};
    for (int k = 2; k <= 7; ++k) rH[k] = FindFirstChild(ws, rnames[k]);
    uintptr_t sittingNpc = FindFirstChild(ws, "SIT");
    uintptr_t sitHum = sittingNpc ? FindFirstChild(sittingNpc, "Humanoid") : 0;
    uintptr_t seat = FindFirstChild(ws, "Seat");
    auto pick = [&](std::vector<uintptr_t> want, std::vector<int> vals) {
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        for (size_t j = 0; j < want.size(); ++j)
            if (want[j]) {
                is.push_back(want[j]);
                vs.push_back(vals[j]);
            }
        return std::pair(is, vs);
    };
    if (sitHum && seat) ScanU64(sitHum, seat, "SeatPart", "Humanoid");
    if (sitHum && sittingNpc) {
        uintptr_t sitRoot = FindFirstChild(sittingNpc, "HumanoidRootPart");
        if (sitRoot) ScanU64(sitHum, sitRoot, "HumanoidRootPart", "Humanoid");
    }
    if (rHumanoid) {
        ScanVec3(rHumanoid, 13.232f, 14.532f, 0.231f, "CameraOffset", "Humanoid");
        ScanFlt(rHumanoid, 734.457f, "HealthDisplayDistance", "Humanoid");
        ScanFlt(rHumanoid, 342.789f, "NameDisplayDistance", "Humanoid");
    }
    {
        auto p = pick({rHumanoid, rH[2], rH[3], rH[4]}, {0, 1, 2, 0});
        if (!p.first.empty()) ScanIntM(p.first, p.second, "DisplayDistanceType", "Humanoid");
    }
    {
        auto p = pick({rH[5], rH[6], rH[7], rH[4]}, {0, 2, 1, 0});
        if (!p.first.empty()) ScanIntM(p.first, p.second, "HealthDisplayType", "Humanoid");
    }
    {
        auto p = pick({rH[5], rH[6], rH[7], rH[4]}, {1, 0, 1, 2});
        if (!p.first.empty()) ScanIntM(p.first, p.second, "NameOcclusion", "Humanoid");
    }
    {
        auto p = pick({hum, rHumanoid, rigHumanoid}, {1, 0, 0});
        if (!p.first.empty()) ScanIntM(p.first, p.second, "RigType", "Humanoid", 0x100, 0x200);
    }
    {
        auto p = pick({rH[3], rH[4], rH[5], rH[6], rH[7]}, {1, 1, 1, 0, 1});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "UseJumpPower", "Humanoid");
    }
    {
        auto p = pick({rH[3], rH[4], rH[5], rH[6], rH[7]}, {1, 1, 0, 0, 0});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "AutomaticScalingEnabled", "Humanoid");
    }
    {
        auto p = pick({rH[4], rH[5], rH[6], rH[7]}, {1, 1, 0, 0});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "BreakJointsOnDeath", "Humanoid");
    }
    {
        auto p = pick({rH[4], rH[5], rH[6], rH[7]}, {1, 0, 1, 1});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "EvaluateStateMachine", "Humanoid");
    }
    {
        auto p = pick({rH[4], rH[5], rH[6], rH[7]}, {0, 1, 0, 1});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "RequiresNeck", "Humanoid");
    }
    {
        auto p = pick({rH[4], rH[5], rH[6], rH[7]}, {1, 0, 0, 1});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "AutoJumpEnabled", "Humanoid");
    }
    {
        auto p = pick({hum, rHumanoid, rigHumanoid, rH[2]}, {1, 0, 0, 1});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "AutoRotate", "Humanoid");
    }
    {
        auto p = pick({rH[4], rH[5], rH[6], rH[7]}, {0, 1, 0, 1});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "Sit", "Humanoid");
    }
    
    {
        uintptr_t mh = FindFirstChild(ws, "MouseHit");
        if (mh) {
            auto ck = GetChildren(mh);
            if (!ck.empty()) {
                std::string nm = GetName(ck[0]);
                float x = 0, y = 0, z = 0;
                if (sscanf(nm.c_str(), "%f,%f,%f", &x, &y, &z) == 3)
                    ScanVec3(hum, x, y, z, "TargetPoint", "Humanoid");
                else
                    LogBad("TargetPoint: bad MouseHit name %s", nm.c_str());
            }
        }
    }
    
    ScanFlt(local, 132.233f, "HealthDisplayDistance", "Player");
    ScanFlt(local, 342.853f, "NameDisplayDistance", "Player");
    {
        
        uintptr_t af = FindFirstChild(ws, "AccountAge");
        if (af) {
            auto ck = GetChildren(af);
            if (!ck.empty()) {
                int age = 0;
                if (sscanf(GetName(ck[0]).c_str(), "%d", &age) == 1)
                    ScanI32(local, age, "AccountAge", "Player", 0, 0x1000);
            }
        }
        uintptr_t uf = FindFirstChild(ws, "UserID");
        if (uf) {
            auto ck = GetChildren(uf);
            if (!ck.empty())
                LogInfo("UserID folder=%s (config %llu)",
                        GetName(ck[0]).c_str(),
                        (unsigned long long)g_userId);
        }
        uintptr_t teams = FindFirstChild(g_dataModel, "Teams");
        uintptr_t meow = teams ? FindFirstChild(teams, "Meow") : 0;
        if (meow) {
            ScanU64(local, meow, "Team", "Player");
            ScanI32(meow, 1015, "BrickColor", "Team", 0, 0x1000);
        }
    }
    if (rHumanoid) ScanStr(rHumanoid, "wow", "DisplayName", "Humanoid", 0x1000);
    {
        
        uintptr_t noob = FindFirstChild(ws, "Noob");
        uintptr_t hNoob = noob ? FindFirstChild(noob, "Humanoid") : 0;
        if (hNoob)
            ScanVec3(hNoob, -0.68840021f, 0.43886614f, 0.57749618f,
                     "MoveDirection", "Humanoid");
        if (rH[2])
            ScanVec3(rH[2], 8282.f, 222.243f, 3.232f, "MoveToPoint",
                     "Humanoid");
        if (rH[4]) {
            uintptr_t pos = FindFirstChild(ws, "Position");
            if (pos) ScanU64(rH[4], pos, "MoveToPart", "Humanoid");
            ScanDblX(rH[4], 8.0, "WalkTimer", "Humanoid", 0.01);
            ScanI32(rH[4], 1056, "FloorMaterial", "Humanoid", 0, 0x1000);
        }
    }
    {
        auto p = pick({hum, rHumanoid, rH[3], rH[4]}, {0, 0, 1, 0});
        if (!p.first.empty()) ScanBoolM(p.first, p.second, "Jump", "Humanoid");
    }
    {
        auto p = pick({rH[4], rH[5], rH[6], rH[7]}, {1, 1, 0, 1});
        if (!p.first.empty())
            ScanBoolM(p.first, p.second, "PlatformStand", "Humanoid");
    }
    if (sitHum && seat)
        ScanU64(seat, sitHum, "Occupant", "Seat");
    {
        
        uintptr_t vs = FindFirstChild(ws, "VehicleSeat");
        if (vs) {
            ScanFlt(vs, 47.321f, "MaxSpeed", "VehicleSeat");
            ScanFlt(vs, 0.892f, "SteerFloat", "VehicleSeat");
            ScanFlt(vs, 0.118f, "ThrottleFloat", "VehicleSeat");
            ScanFlt(vs, 24.234f, "Torque", "VehicleSeat");
            ScanFlt(vs, 2.992f, "TurnSpeed", "VehicleSeat");
        }
    }
    {
        
        uintptr_t bp = FindFirstChild(local, "Backpack");
        uintptr_t t[4] = {0};
        const char* tn[4] = {"Tool", "Tool2", "Tool3", "Tool4"};
        for (int k = 0; k < 4; ++k)
            t[k] = bp ? FindFirstChild(bp, tn[k]) : 0;
        if (t[0]) {
            ScanStr(t[0], "meow", "Tooltip", "Tool", 0x1000);
            ScanStr(t[0], "rbxassetid://73879578225090", "TextureId", "Tool",
                    0x2000);
            ScanVec3(t[0], 67.f, 69.f, 420.f, "Grip", "Tool");
        }
        {
            
            
            
            
            
            auto collect = [&](std::vector<int> w) {
                std::vector<uintptr_t> out;
                std::vector<uintptr_t> is;
                for (int k = 0; k < 4; ++k)
                    if (t[k]) is.push_back(t[k]);
                if (is.empty()) return out;
                for (uintptr_t i = 0; i < 0x1000; ++i) {
                    bool ok = true;
                    for (size_t j = 0; j < is.size(); ++j) {
                        char b = Read<char>(is[j] + i);
                        if ((b != 0) != (w[j] != 0)) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) out.push_back(i);
                }
                return out;
            };
            std::vector<uintptr_t> mEn = collect({1, 0, 1, 0});
            std::vector<uintptr_t> mCb = collect({0, 1, 0, 1});
            std::vector<uintptr_t> mMa = collect({1, 0, 1, 1});
            std::vector<uintptr_t> mRh = collect({1, 1, 0, 1});
            bool clustered = false;
            
            for (uintptr_t base : mEn) {
                uintptr_t cb = 0, ma = 0, rh = 0;
                for (uintptr_t x : mCb)
                    if (x >= base && x < base + 8) {
                        cb = x;
                        break;
                    }
                for (uintptr_t x : mMa)
                    if (x >= base && x < base + 8) {
                        ma = x;
                        break;
                    }
                for (uintptr_t x : mRh)
                    if (x >= base && x < base + 8) {
                        rh = x;
                        break;
                    }
                if (cb && ma && rh) {
                    Emit("Enabled", "Tool", base, "bool");
                    Emit("CanBeDropped", "Tool", cb, "bool");
                    Emit("ManualActivationOnly", "Tool", ma, "bool");
                    Emit("RequiresHandle", "Tool", rh, "bool");
                    LogInfo("Tool flags clustered @+%#lx..+%#lx",
                            (unsigned long)base, (unsigned long)(base + 8));
                    clustered = true;
                    break;
                }
            }
            if (!clustered)
                LogBad("MISS   Tool flags (no 8B cluster; tools not in rig "
                       "state?)");
        }
    }
    {
        
        uintptr_t cl = FindFirstChild(ws, "Clothing");
        if (cl) {
            ScanStr(cl, "rbxassetid://73879578225090", "Template", "Clothing",
                    0x2000);
            ScanVec3(cl, 17.f, 199.f, 255.f, "Color3", "Clothing");
        }
        uintptr_t cm = FindFirstChild(ws, "CharacterMesh");
        uintptr_t cm2 = FindFirstChild(ws, "CharacterMesh2");
        if (cm) {
            ScanStr(cm, "rbxassetid://3242", "BaseTextureId", "CharacterMesh",
                    0x1000);
            ScanStr(cm, "rbxassetid://2732", "OverlayTextureId",
                    "CharacterMesh", 0x1000);
            ScanStr(cm, "rbxassetid://5867", "MeshId", "CharacterMesh",
                    0x1000);
        }
        {
            auto p = pick({cm, cm2}, {3, 5});
            if (!p.first.empty())
                ScanIntM(p.first, p.second, "BodyPart", "CharacterMesh");
        }
    }
    {
        
        uintptr_t st = FindFirstChild(g_dataModel, "Stats");
        uintptr_t ps = st ? FindFirstChild(st, "PerformanceStats") : 0;
        uintptr_t ping = ps ? FindFirstChild(ps, "Ping") : 0;
        if (ping) {
            
            
            
            uintptr_t cands[4] = {0};
            int nc = 0;
            for (uintptr_t i = 0x50; i < 0x500 && nc < 4; i += 1) {
                double v = Read<double>(ping + i);
                if (v >= 30.0 && v <= 60.0) cands[nc++] = i;
            }
            usleep(100000);
            bool done = false;
            for (int k = 0; k < nc && !done; ++k) {
                double v = Read<double>(ping + cands[k]);
                if (v >= 30.0 && v <= 60.0) {
                    Emit("Value", "StatsItem", cands[k], "double");
                    LogInfo("StatsItem ping stable @+%#lx (%.1fms)",
                            (unsigned long)cands[k], v);
                    done = true;
                }
            }
            if (!done && nc > 0) {
                
                
                Emit("Value", "StatsItem", cands[0], "double");
                LogInfo("StatsItem ping VOLATILE @+%#lx (first-match)",
                        (unsigned long)cands[0]);
                done = true;
            }
            if (!done)
                LogBad("MISS   %-28s %-14s (no ping 30..60 at all)", "Value",
                       "StatsItem");
        }
    }
    return true;
}
}  
