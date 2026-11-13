#include "dump.h"

#include <algorithm>
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
#include "rtti.h"
#include "scanner.h"

namespace Dump {

bool DetectResolution() {
    FILE* p = popen("xrandr 2>/dev/null", "r");
    if (!p) return false;
    char* ln = nullptr;
    size_t cp = 0;
    bool ok = false;
    while (getline(&ln, &cp, p) != -1) {
        if (!strchr(ln, '*')) continue;
        int w = 0, h = 0;
        if (sscanf(ln, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            g_winW = w;
            g_winH = h;
            ok = true;
            break;
        }
    }
    pclose(p);
    free(ln);
    return ok;
}

struct Mat3 {
    float d[9];
};

static void EulerDeg(const Mat3& m, float& x, float& y, float& z) {
    float c6 = m.d[6] < -1.f ? -1.f : (m.d[6] > 1.f ? 1.f : m.d[6]);
    y = asinf(c6);
    if (fabsf(m.d[6]) < 0.9999f) {
        x = atan2f(-m.d[7], m.d[8]);
        z = atan2f(-m.d[3], m.d[0]);
    } else {
        x = 0.f;
        z = atan2f(m.d[1], m.d[4]);
    }
    x *= 180.f / 3.14159265358979323846f;
    y *= 180.f / 3.14159265358979323846f;
    z *= 180.f / 3.14159265358979323846f;
}

static bool MatSane(const Mat3& m) {
    for (int i = 0; i < 9; ++i)
        if (std::isnan(m.d[i]) || std::isinf(m.d[i])) return false;
    return true;
}




static bool MatOrtho(const Mat3& m) {
    const float* r[3] = {m.d, m.d + 3, m.d + 6};
    for (int i = 0; i < 3; ++i) {
        float l =
            r[i][0] * r[i][0] + r[i][1] * r[i][1] + r[i][2] * r[i][2];
        if (fabsf(l - 1.0f) > 0.002f) return false;
        float c = m.d[i] * m.d[i] + m.d[i + 3] * m.d[i + 3] +
                  m.d[i + 6] * m.d[i + 6];
        if (fabsf(c - 1.0f) > 0.002f) return false;
    }
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j) {
            float d = r[i][0] * r[j][0] + r[i][1] * r[j][1] +
                      r[i][2] * r[j][2];
            if (fabsf(d) > 0.002f) return false;
        }
    return true;
}

bool Camera() {
    if (DetectResolution())
        LogInfo("window resolution: %dx%d (xrandr)", g_winW, g_winH);
    else
        LogInfo("window resolution: %dx%d (config)", g_winW, g_winH);
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    if (!ws) {
        LogBad("Camera FAILED: no Workspace");
        return false;
    }
    uintptr_t cam = FindFirstChild(ws, "Camera");
    LogInfo("Camera=%#lx", cam);
    if (!cam) return false;
    ScanU64(ws, cam, "CurrentCamera", "Workspace");
    
    uintptr_t players = FindFirstChild(g_dataModel, "Players");
    uintptr_t local = 0;
    if (players) {
        for (uintptr_t c : GetChildren(players)) {
            for (uintptr_t i = 0; i < 0x1000; i += 8)
                if (Read<uint64_t>(c + i) == g_userId) {
                    local = c;
                    break;
                }
            if (local) break;
        }
        if (!local) {
            auto kids = GetChildren(players);
            if (!kids.empty()) local = kids[0];
        }
    }
    uintptr_t hum = 0;
    if (local) {
        uintptr_t ch = FindFirstChild(ws, GetName(local));
        if (ch) hum = FindFirstChild(ch, "Humanoid");
    }
    ScanU64(cam, hum, "CameraSubject", "Camera");
    uintptr_t posOff =
        ScanVec3(cam, g_camX, g_camY, g_camZ, "Position", "Camera");
    
    
    
    uintptr_t rotOff = 0;
    {
        std::vector<uintptr_t> ortho;
        for (uintptr_t i = 0; i < 0x1000; i += 4) {
            Mat3 m;
            if (!ReadBytes(cam + i, (char*)&m, sizeof(m))) continue;
            if (MatSane(m) && MatOrtho(m)) ortho.push_back(i);
        }
        if (ortho.size() == 1) {
            rotOff = ortho[0];
            LogInfo("Rotation ortho-single @+%#lx (pose-free)",
                    (unsigned long)rotOff);
        } else {
            for (uintptr_t i : ortho) {
                Mat3 m;
                ReadBytes(cam + i, (char*)&m, sizeof(m));
                float x, y, z;
                EulerDeg(m, x, y, z);
                if (fabsf(x - g_camPitch) <= 0.1f) {
                    rotOff = i;
                    break;
                }
            }
            if (rotOff)
                LogInfo("Rotation ortho shortlist %zu, pitch pick @+%#lx",
                        ortho.size(), (unsigned long)rotOff);
        }
    }
    if (rotOff)
        Emit("Rotation", "Camera", rotOff, "Matrix3x3");
    else
        LogBad("CameraRotation dump stalled, try rejoining the game.");
    if (!posOff && rotOff) {
        
        
        
        
        float p[3] = {0, 0, 0};
        bool sane = ReadBytes(cam + rotOff + 0x24, (char*)p, 12) &&
                    std::isfinite(p[0]) && std::isfinite(p[1]) &&
                    std::isfinite(p[2]) && fabsf(p[0]) < 1e5f &&
                    fabsf(p[1]) < 1e5f && fabsf(p[2]) < 1e5f &&
                    (fabsf(p[0]) + fabsf(p[1]) + fabsf(p[2]) > 1.0f);
        if (sane) {
            Emit("Position", "Camera", rotOff + 0x24, "Vector3");
            LogInfo("Position STRUCT @+0x%lx (%.2f,%.2f,%.2f) [rotation-relative]",
                    (unsigned long)(rotOff + 0x24), p[0], p[1], p[2]);
            posOff = rotOff + 0x24;
        } else {
            LogBad("MISS   %-28s %-14s (rotation-relative bytes insane)",
                   "Position", "Camera");
        }
    }
    ScanFlt(cam, g_fov, "FieldOfView", "Camera");
    ScanFlt(cam, 1.f / (2.f * tanf(g_fov / 2.f)), "ImagePlaneDepth", "Camera");
    if (local) {
        uintptr_t minOff =
            ScanFlt(local, g_minZoom, "MinZoomDistance", "Player");
        ScanFlt(local, g_maxZoom, "MaxZoomDistance", "Player");
        ScanI32(local, g_camMode, "CameraMode", "Player", minOff,
                minOff ? 0x1000 : 0);
        ScanI32(cam, g_camType, "CameraType", "Camera", 0x100, 0x1000);
    }
    ScanI16(cam, (int16_t)g_winW, "Viewport", "Camera");
    ScanVec2(cam, (float)g_winW, (float)g_winH, "ViewportSize", "Camera");
    return true;
}
}  
