#include "dump.h"

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
static bool U8Eq(uintptr_t addr, unsigned char r, unsigned char g,
                 unsigned char b) {
    char t[3];
    if (!ReadBytes(addr, t, 3)) return false;
    return (unsigned char)t[0] == r && (unsigned char)t[1] == g &&
           (unsigned char)t[2] == b;
}

bool Workspace() {
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    if (!ws) {
        LogBad("Workspace FAILED: no Workspace kid");
        return false;
    }
    
    uintptr_t world = 0;
    for (uintptr_t po = 0; po < 0x1000 && !world; po += 1) {
        uintptr_t p2 = Read<uintptr_t>(ws + po);
        if (!IsValid(p2)) continue;
        for (uintptr_t i = 0; i < 0x300; i += 4) {
            float g = Read<float>(p2 + i);
            if (!std::isnan(g) && fabsf(g - 165.231f) <= 0.001f) {
                Emit("World", "Workspace", po, "unsigned __int64");
                Emit("Gravity", "World", i, "float");
                world = p2;
                break;
            }
        }
    }
    if (!world) {
        LogBad("Workspace FAILED: no World/Gravity");
        return false;
    }
    ScanFlt(world, 240.f, "worldStepsPerSec", "World", 0x600, 0x1000);
    ScanFlt(world, -231.232f, "FallenPartsDestroyHeight", "World");
    ScanFlt(ws, 165.231f, "ReadOnlyGravity", "Workspace");
    {
        
        uintptr_t gf = FindFirstChild(ws, "GameTime");
        if (gf) {
            auto ck = GetChildren(gf);
            if (!ck.empty()) {
                double t = 0;
                if (sscanf(GetName(ck[0]).c_str(), "%lf", &t) == 1)
                    ScanDblX(ws, t, "DistributedGameTime", "Workspace", 2.0);
            }
        }
    }
    uintptr_t air = 0;
    for (uintptr_t i = 0x50; i < 0x250 && !air; i += 1) {
        uintptr_t a = Read<uintptr_t>(world + i);
        if (!IsValid(a)) continue;
        for (uintptr_t j = 0x8; j < 0x100; j += 4) {
            float d = Read<float>(a + j);
            if (!std::isnan(d) && fabsf(d - 0.201f) <= 0.001f) {
                Emit("AirProperties", "World", i, "unsigned __int64");
                Emit("AirDensity", "AirProperties", j, "float");
                air = a;
                break;
            }
        }
    }
    if (air)
        ScanVec3(air, 154.632f, 155.632f, 156.632f, "GlobalWind",
                 "AirProperties");
    {
        
        uintptr_t pf = FindFirstChild(ws, "PrimCount");
        int want = -1;
        if (pf) {
            auto ck = GetChildren(pf);
            if (!ck.empty()) sscanf(GetName(ck[0]).c_str(), "%d", &want);
        }
        if (want > 0) {
            for (uintptr_t p2 = 0x150; p2 < 0x300; p2 += 8) {
                uintptr_t pp2 = Read<uintptr_t>(world + p2);
                if (!IsValid(pp2)) continue;
                int total = 0;
                for (uintptr_t k = 0; k < 0xFFFFFF; k += 8) {
                    uintptr_t prim = Read<uintptr_t>(pp2 + k);
                    if (!prim || !IsValid(prim)) break;
                    if (Read<int>(prim + 0x8) == 0x6) ++total;
                }
                if (total == want) {
                    Emit("Primitives", "World", p2, "unsigned __int64");
                    break;
                }
                if (p2 == 0x2F8)
                    LogBad("MISS   %-28s %-14s (count %d)", "Primitives",
                           "World", want);
            }
        }
        uintptr_t af = FindFirstChild(ws, "AllPrimCount");
        if (af) {
            auto ck = GetChildren(af);
            if (!ck.empty()) {
                int all = 0;
                if (sscanf(GetName(ck[0]).c_str(), "%d", &all) == 1)
                    ScanI32(g_dataModel, all, "PrimitiveCount", "DataModel",
                            0, 0x1000);
            }
        }
    }
    uintptr_t terrain = FindFirstChild(ws, "Terrain");
    if (terrain) {
        ScanFlt(terrain, 0.723f, "GrassLength", "Terrain");
        ScanFlt(terrain, 0.652f, "WaterReflectance", "Terrain");
        ScanFlt(terrain, 0.323f, "WaterTransparency", "Terrain");
        ScanFlt(terrain, 0.123f, "WaterWaveSize", "Terrain");
        ScanFlt(terrain, 35.234f, "WaterWaveSpeed", "Terrain");
        ScanVec3(terrain, 0.047f, 0.329f, 0.361f, "WaterColor", "Terrain");
        
        uintptr_t matOff = 0;
        for (uintptr_t i = 0; i < 0x1000 && !matOff; i += 1) {
            uintptr_t colors = Read<uintptr_t>(terrain + i);
            if (!IsValid(colors)) continue;
            for (uintptr_t j = 0x10; j < 0x50; j += 1)
                if (U8Eq(colors + j, 80, 84, 84)) {
                    matOff = i;
                    break;
                }
        }
        if (matOff) {
            Emit("MaterialColors", "Terrain", matOff, "unsigned __int64");
            uintptr_t mc = Read<uintptr_t>(terrain + matOff);
            char blk[0x1000];
            if (ReadBytes(mc, blk, sizeof(blk))) {
                struct Mat {
                    const char* n;
                    unsigned char r, g, b;
                };
                const Mat mats[] = {
                    {"Asphalt", 80, 84, 84}, {"Basalt", 75, 74, 74},
                    {"Brick", 138, 97, 73}, {"Cobblestone", 134, 134, 118},
                    {"Concrete", 152, 152, 152}, {"CrackedLava", 255, 24, 67},
                    {"Glacier", 221, 228, 229}, {"Grass", 111, 126, 62},
                    {"Ground", 140, 130, 104}, {"Ice", 204, 210, 223},
                    {"LeafyGrass", 106, 134, 64},
                    {"Limestone", 255, 243, 192}, {"Mud", 121, 112, 98},
                    {"Pavement", 143, 144, 135}, {"Rock", 99, 100, 102},
                    {"Salt", 255, 255, 254}, {"Sand", 207, 203, 167},
                    {"Sandstone", 148, 124, 95}, {"Slate", 88, 89, 86},
                    {"Snow", 235, 253, 255}, {"WoodPlanks", 172, 148, 108},
                };
                for (auto& m : mats) {
                    bool done = false;
                    for (size_t k = 0; k + 3 < sizeof(blk) && !done; ++k)
                        if ((unsigned char)blk[k] == m.r &&
                            (unsigned char)blk[k + 1] == m.g &&
                            (unsigned char)blk[k + 2] == m.b) {
                            Emit(m.n, "MaterialColors", k,
                                 "unsigned __int64");
                            done = true;
                        }
                    if (!done)
                        LogBad("MISS   %-28s %-14s", m.n, "MaterialColors");
                }
            }
        } else {
            LogBad("MISS   %-28s %-14s", "MaterialColors", "Terrain");
        }
    }
    auto pickB = [&](std::vector<uintptr_t> want, std::vector<int> vals) {
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        for (size_t j = 0; j < want.size(); ++j)
            if (want[j]) {
                is.push_back(want[j]);
                vs.push_back(vals[j]);
            }
        return std::pair(is, vs);
    };
    {
        
        uintptr_t s1 = FindFirstChild(ws, "Sound");
        uintptr_t s2 = FindFirstChild(ws, "Sound2");
        uintptr_t s3 = FindFirstChild(ws, "Sound3");
        uintptr_t s4 = FindFirstChild(ws, "Sound4");
        if (s1) {
            ScanStr(s1, "rbxassetid://skibidi", "SoundId", "Sound", 0x1000);
            ScanFlt(s1, 2312.321f, "RollOffMaxDistance", "Sound");
            ScanFlt(s1, 2423.213f, "RollOffMinDistance", "Sound");
            ScanFlt(s1, 1.237f, "PlaybackSpeed", "Sound");
            ScanFlt(s1, 0.523f, "Volume", "Sound");
            uintptr_t sg = FindFirstChild(ws, "SoundGroup");
            if (sg) ScanU64(s1, sg, "SoundGroup", "Sound");
        }
        {
            auto p = pickB({s1, s2, s3, s4}, {0, 1, 0, 1});
            if (!p.first.empty())
                ScanBoolM(p.first, p.second, "IsPlaying", "Sound");
        }
        {
            auto p = pickB({s1, s2, s3, s4}, {0, 1, 1, 0});
            if (!p.first.empty())
                ScanBoolM(p.first, p.second, "Looped", "Sound");
        }
    }
    {
        
        uintptr_t l1 = FindFirstChild(ws, "SpawnLocation");
        uintptr_t l2 = FindFirstChild(ws, "SpawnLocation2");
        uintptr_t l3 = FindFirstChild(ws, "SpawnLocation3");
        
        
        
        
        uintptr_t anchor = 0;
        {
            auto ints = [&](std::vector<int> w, const char* nm) {
                for (uintptr_t i = 0x100; i < 0x300; ++i) {
                    bool ok = true;
                    if (!l1 || !l2) return (uintptr_t)0;
                    if (Read<int32_t>(l1 + i) != w[0] ||
                        Read<int32_t>(l2 + i) != w[1])
                        ok = false;
                    if (ok) {
                        Emit(nm, "SpawnLocation", i, "int");
                        return i;
                    }
                }
                LogBad("MISS   %-28s %-14s", nm, "SpawnLocation");
                return (uintptr_t)0;
            };
            uintptr_t ff = ints({4345, 4350}, "ForcefieldDuration");
            uintptr_t tc = ints({307, 315}, "TeamColor");
            if (ff && tc && (ff > tc ? ff - tc : tc - ff) < 0x20)
                anchor = ff < tc ? ff : tc;
            else if (ff)
                anchor = ff;
        }
        auto pickB = [&](std::vector<uintptr_t> want, std::vector<int> vals) {
            std::vector<uintptr_t> is;
            std::vector<int> vs;
            for (size_t j = 0; j < want.size(); ++j)
                if (want[j]) {
                    is.push_back(want[j]);
                    vs.push_back(vals[j]);
                }
            return std::pair(is, vs);
        };
        
        
        auto scanAnchored = [&](const char* nm, std::vector<int> w) {
            auto p = pickB({l1, l2, l3}, w);
            if (p.first.empty()) return;
            uintptr_t s = anchor ? (anchor > 16 ? anchor - 16 : 0) : 0x100;
            uintptr_t e = anchor ? anchor + 32 : 0x200;
            for (uintptr_t i = s; i < e; ++i) {
                bool ok = true;
                for (size_t j = 0; j < p.first.size(); ++j) {
                    char b = Read<char>(p.first[j] + i);
                    if ((b != 0) != (p.second[j] != 0)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    Emit(nm, "SpawnLocation", i, "bool");
                    if (!anchor)
                        LogInfo("WIDE   %-28s %-14s (no int anchor)", nm,
                                "SpawnLocation");
                    return;
                }
            }
            LogBad("MISS   %-28s %-14s (anchored)", nm, "SpawnLocation");
        };
        scanAnchored("AllowTeamChangeOnTouch", {1, 1, 0});
        scanAnchored("Enabled", {1, 0, 1});
        scanAnchored("Neutral", {0, 1, 0});
    }
    {
        
        uintptr_t a1 = FindFirstChild(ws, "SurfaceAppearance");
        uintptr_t a2 = FindFirstChild(ws, "SurfaceAppearance2");
        uintptr_t a3 = FindFirstChild(ws, "SurfaceAppearance3");
        {
            auto p = pickB({a1, a2, a3}, {0, 2, 1});
            if (!p.first.empty()) {
                for (uintptr_t i = 0x100; i < 0x200; ++i) {
                    bool ok = true;
                    for (size_t j = 0; j < p.first.size(); ++j)
                        if (Read<int32_t>(p.first[j] + i) != p.second[j]) {
                            ok = false;
                            break;
                        }
                    if (ok) {
                        Emit("AlphaMode", "SurfaceAppearance", i, "int");
                        break;
                    }
                    if (i == 0x1FF)
                        LogBad("MISS   %-28s %-14s", "AlphaMode",
                               "SurfaceAppearance");
                }
            }
        }
        if (a1) {
            ScanVec3(a1, 45.f, 172.f, 102.f, "Color", "SurfaceAppearance");
            ScanStr(a1, "rbxassetid://73879578225090", "ColorMap",
                    "SurfaceAppearance", 0x2000);
            ScanStr(a1, "rbxassetid://73879578225091", "EmissiveMaskContent",
                    "SurfaceAppearance", 0x2000);
            ScanFlt(a1, 3.532f, "EmissiveStrength", "SurfaceAppearance");
            ScanVec3(a1, 18.f, 100.f, 231.f, "EmissiveTint",
                     "SurfaceAppearance");
            ScanStr(a1, "rbxassetid://73879578225092", "MetalnessMap",
                    "SurfaceAppearance", 0x2000);
            ScanStr(a1, "rbxassetid://73879578225093", "NormalMap",
                    "SurfaceAppearance", 0x2000);
            ScanStr(a1, "rbxassetid://73879578225094", "RoughnessMap",
                    "SurfaceAppearance", 0x2000);
        }
    }
    {
        
        uintptr_t pe = FindFirstChild(ws, "ParticleEmitter");
        if (pe) {
            ScanFlt(pe, 1.993f, "Brightness", "ParticleEmitter");
            ScanFlt(pe, 0.872f, "LightEmission", "ParticleEmitter");
            ScanFlt(pe, 1.332f, "LightInfluence", "ParticleEmitter");
            ScanStr(pe, "rbxasset://textures/particles/sparkles_main.dds",
                    "Texture", "ParticleEmitter", 0x2000);
            ScanFlt(pe, 832.235f, "ZOffset", "ParticleEmitter");
            ScanVec2(pe, 5.32f, 10.88f, "Lifetime", "ParticleEmitter");
            ScanFlt(pe, 33.884f, "Rate", "ParticleEmitter");
            ScanFlt(pe, 88.243f, "Rotation", "ParticleEmitter");
            ScanFlt(pe, 23.856f, "RotSpeed", "ParticleEmitter");
            ScanFlt(pe, 2.123f, "Speed", "ParticleEmitter");
            ScanVec2(pe, 4.233f, 8.354f, "SpreadAngle", "ParticleEmitter");
            ScanVec3(pe, 90.394f, 32.234f, 12.857f, "Acceleration",
                     "ParticleEmitter");
            ScanFlt(pe, 0.376f, "Drag", "ParticleEmitter");
            ScanFlt(pe, 0.728f, "TimeScale", "ParticleEmitter");
            ScanFlt(pe, 0.238f, "VelocityInheritance", "ParticleEmitter");
        }
    }
    {
        
        uintptr_t bm = FindFirstChild(ws, "Beam");
        if (bm) {
            ScanFlt(bm, 1.775f, "Brightness", "Beam");
            ScanFlt(bm, 0.392f, "LightEmission", "Beam");
            ScanFlt(bm, 0.745f, "LightInfluence", "Beam");
            ScanStr(bm, "rbxassetid://73879578225090", "Texture", "Beam",
                    0x2000);
            ScanFlt(bm, 1.625f, "TextureLength", "Beam");
            ScanFlt(bm, 0.642f, "TextureSpeed", "Beam");
            ScanFlt(bm, 0.975f, "ZOffset", "Beam");
            uintptr_t ba1 = FindFirstChild(ws, "BeamAttach1");
            uintptr_t ba2 = FindFirstChild(ws, "BeamAttach2");
            if (ba1) ScanU64(bm, ba1, "Attachment0", "Beam");
            if (ba2) ScanU64(bm, ba2, "Attachment1", "Beam");
            ScanFlt(bm, 3.324f, "CurveSize0", "Beam");
            ScanFlt(bm, 7.885f, "CurveSize1", "Beam");
            ScanFlt(bm, 0.328f, "Width0", "Beam");
            ScanFlt(bm, 5.775f, "Width1", "Beam");
        }
    }
    return true;
}
}  
