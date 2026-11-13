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


bool Lighting() {
    uintptr_t li = FindFirstChild(g_dataModel, "Lighting");
    if (!li) {
        LogBad("Lighting FAILED: no Lighting kid");
        return false;
    }
    ScanFlt(li, -0.00897f, "ClockTime", "Lighting", 0, 0x1000, 0.0001f);
    ScanFlt(li, 2.54f, "Brightness", "Lighting");
    ScanFlt(li, 0.872f, "EnvironmentDiffuseScale", "Lighting");
    ScanFlt(li, 0.233f, "EnvironmentSpecularScale", "Lighting");
    ScanFlt(li, 722.1f, "FogStart", "Lighting", 0, 0x1000, 0.1f);
    ScanFlt(li, 100000.f, "FogEnd", "Lighting", 0, 0x1000, 0.1f);
    ScanVec3(li, 0.752941f, 0.752941f, 0.752941f, "FogColor", "Lighting",
             0.01f);
    ScanVec3(li, 0.541f, 0.541f, 0.541f, "Ambient", "Lighting", 0.01f);
    ScanVec3(li, 0.502f, 0.502f, 0.502f, "OutdoorAmbient", "Lighting", 0.01f);
    ScanVec3(li, 0.121569f, 0.705882f, 0.207843f, "ColorShift_Top",
             "Lighting", 0.01f);
    ScanVec3(li, 0.070588f, 0.062745f, 0.027451f, "ColorShift_Bottom",
             "Lighting", 0.01f);
    ScanFlt(li, 2.13f, "ExposureCompensation", "Lighting");
    ScanFlt(li, 115.231f, "GeographicLatitude", "Lighting", 0, 0x1000, 1.0f);
    
    for (uintptr_t i = 0x120; i < 0x1000; i += 4) {
        float v[3];
        if (!ReadBytes(li + i, (char*)v, 12)) continue;
        bool ok = true;
        for (int k = 0; k < 3; ++k)
            if (std::isnan(v[k]) || v[k] < 0.001f || v[k] > 0.999f ||
                fabsf(v[k] - 0.75f) > 0.01f)
                ok = false;
        if (ok) {
            Emit("LightColor", "Lighting", i, "Color3");
            break;
        }
        if (i >= 0xFFC) LogBad("MISS   %-28s %-14s", "LightColor",
                               "Lighting");
    }
    
    int grads = 0;
    for (uintptr_t i = 0; i < 0x1000 && grads < 2; i += 4) {
        float v[3];
        if (!ReadBytes(li + i, (char*)v, 12)) continue;
        if (v[0] == 1.0f && v[1] == 1.0f && v[2] == 1.0f) {
            Emit(grads == 0 ? "GradientTop" : "GradientBottom", "Lighting",
                 i, "Color3");
            ++grads;
        }
    }
    if (grads < 2)
        LogBad("MISS   %-28s %-14s (found %d/2)", "GradientTop/Bottom",
               "Lighting", grads);
    ScanVec3(li, -0.0151139f, 0.026178f, 0.999543f, "MoonPosition",
             "Lighting", 0.01f);
    ScanVec3(li, 0.0151139f, -0.026178f, 0.999543f, "SunPosition",
             "Lighting", 0.01f);
    uintptr_t sky = FindFirstChild(li, "Sky");
    if (sky) {
        ScanU64(li, sky, "Sky", "Lighting");
        ScanFlt(sky, 1.0f, "SunAngularSize", "Sky", 0, 0x1000, 1.0f);
        ScanFlt(sky, 1.0f, "MoonAngularSize", "Sky", 0, 0x1000, 1.0f);
        ScanVec3(sky, 123.f, 22.f, 83.f, "SkyboxOrientation", "Sky", 1.0f);
        ScanI32(sky, 3000, "StarCount", "Sky", 0, 0x1000);
        ScanStr(sky, "sky/sun.jpg", "SunTextureId", "Sky", 0x2000);
        ScanStr(sky, "sky/moon.jpg", "MoonTextureId", "Sky", 0x2000);
    }
    uintptr_t at = FindFirstChild(li, "Atmosphere");
    if (at) {
        ScanFlt(at, 0.315f, "Density", "Atmosphere");
        ScanFlt(at, 0.243f, "Offset", "Atmosphere");
        ScanVec3(at, 0.541f, 0.541f, 0.541f, "Color", "Atmosphere", 0.01f);
        ScanVec3(at, 18.f, 16.f, 7.f, "Decay", "Atmosphere", 0.01f);
        ScanFlt(at, 0.125f, "Glare", "Atmosphere");
        ScanFlt(at, 0.372f, "Haze", "Atmosphere");
    }
    auto blooms = [&](const char* n) {
        return FindFirstChild(li, n);
    };
    uintptr_t b1 = blooms("Bloom"), b2 = blooms("Bloom2"), b3 = blooms("Bloom3"),
              b4 = blooms("Bloom4");
    if (b1) {
        ScanFlt(b1, 0.652f, "Intensity", "BloomEffect");
        ScanFlt(b1, 5.123f, "Size", "BloomEffect");
        ScanFlt(b1, 4.231f, "Threshold", "BloomEffect");
    }
    {
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        int want[4] = {1, 0, 0, 0};
        uintptr_t bs[4] = {b1, b2, b3, b4};
        for (int k = 0; k < 4; ++k)
            if (bs[k]) {
                is.push_back(bs[k]);
                vs.push_back(want[k]);
            }
        if (!is.empty()) ScanBoolM(is, vs, "Enabled", "BloomEffect");
    }
    uintptr_t dof = FindFirstChild(li, "DepthOfField");
    if (dof) {
        ScanFlt(dof, 93.82f, "FocusDistance", "DepthOfFieldEffect");
        ScanFlt(dof, 0.259f, "FarIntensity", "DepthOfFieldEffect");
        ScanFlt(dof, 0.173f, "NearIntensity", "DepthOfFieldEffect");
        ScanFlt(dof, 9.875f, "InFocusRadius", "DepthOfFieldEffect");
    }
    uintptr_t sr = FindFirstChild(li, "SunRays");
    if (sr) {
        ScanFlt(sr, 0.296f, "Intensity", "SunRaysEffect");
        ScanFlt(sr, 0.827f, "Spread", "SunRaysEffect");
    }
    uintptr_t cc = FindFirstChild(li, "ColorCorrection");
    if (cc) {
        ScanFlt(cc, 0.124f, "Brightness", "ColorCorrectionEffect");
        ScanFlt(cc, 0.132f, "Contrast", "ColorCorrectionEffect");
        ScanVec3(cc, 126.f, 139.f, 82.f, "TintColor", "ColorCorrectionEffect",
                 0.01f);
    }
    {
        uintptr_t g1 = FindFirstChild(li, "ColorGrading");
        uintptr_t g2 = FindFirstChild(li, "ColorGrading2");
        uintptr_t g3 = FindFirstChild(li, "ColorGrading3");
        uintptr_t g4 = FindFirstChild(li, "ColorGrading4");
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        int want[4] = {0, 0, 1, 1};
        uintptr_t gs[4] = {g1, g2, g3, g4};
        for (int k = 0; k < 4; ++k)
            if (gs[k]) {
                is.push_back(gs[k]);
                vs.push_back(want[k]);
            }
        if (!is.empty())
            ScanIntM(is, vs, "TonemapperPreset", "ColorGradingEffect");
    }
    uintptr_t bl = FindFirstChild(li, "Blur");
    if (bl) ScanFlt(bl, 32.213f, "Size", "BlurEffect");
    return true;
}
}  
