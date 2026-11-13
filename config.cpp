#include "config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "globals.h"
#include "log.h"

static std::vector<pid_t> AllPids() {
    std::vector<pid_t> out;
    DIR* d = opendir("/proc");
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        out.push_back(atoi(e->d_name));
    }
    closedir(d);
    return out;
}

static std::string CommOf(pid_t p) {
    char path[64], buf[64] = {};
    snprintf(path, sizeof(path), "/proc/%d/comm", p);
    FILE* f = fopen(path, "r");
    if (!f) return "";
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return "";
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = 0;
    return buf;
}

pid_t FindMain() {
    pid_t best = -1;
    long bestMaps = -1;
    for (pid_t p : AllPids()) {
        std::string c = CommOf(p);
        if (c == "Main") return p;
        if (c != "sober") continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/maps", p);
        FILE* m = fopen(path, "r");
        if (!m) continue;
        long n = 0;
        char* ln = nullptr;
        size_t cp = 0;
        while (getline(&ln, &cp, m) != -1) ++n;
        fclose(m);
        free(ln);
        if (n > bestMaps) {
            bestMaps = n;
            best = p;
        }
    }
    return best;
}


static std::string CfgStr(const std::string& txt, const char* key) {
    
    std::string k = std::string("\"") + key + "\"";
    size_t p = txt.find(k);
    if (p == std::string::npos) return "";
    p = txt.find(':', p);
    if (p == std::string::npos) return "";
    size_t q1 = txt.find('"', p);
    if (q1 == std::string::npos) return "";
    size_t q2 = txt.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return txt.substr(q1 + 1, q2 - q1 - 1);
}

static uint64_t CfgU64(const std::string& txt, const char* key, uint64_t dflt) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = txt.find(k);
    if (p == std::string::npos) return dflt;
    p = txt.find(':', p);
    if (p == std::string::npos) return dflt;
    return strtoull(txt.c_str() + p + 1, nullptr, 10);
}

static float CfgFlt(const std::string& txt, const char* key, float dflt) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = txt.find(k);
    if (p == std::string::npos) return dflt;
    p = txt.find(':', p);
    if (p == std::string::npos) return dflt;
    return strtof(txt.c_str() + p + 1, nullptr);
}

static int CfgInt(const std::string& txt, const char* key, int dflt) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = txt.find(k);
    if (p == std::string::npos) return dflt;
    p = txt.find(':', p);
    if (p == std::string::npos) return dflt;
    return atoi(txt.c_str() + p + 1);
}

bool LoadConfig(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        LogInfo("[config] %s not found, using built-ins", path);
        return true;
    }
    char* buf = nullptr;
    size_t cap = 0;
    std::string txt;
    ssize_t n;
    while ((n = getline(&buf, &cap, f)) != -1) {
        std::string ln(buf, n);
        size_t c = ln.find("//");  
        if (c != std::string::npos && (c == 0 || ln[c - 1] != ':'))
            ln = ln.substr(0, c);
        txt += ln;
    }
    fclose(f);
    free(buf);
    std::string dn = CfgStr(txt, "userDisplayName");
    if (!dn.empty()) g_displayName = dn;
    std::string loc = CfgStr(txt, "localeId");
    if (!loc.empty()) g_localeId = loc;
    std::string dp = CfgStr(txt, "dumpedRobloxPath");
    if (dp.empty()) dp = CfgStr(txt, "robloxPath");
    if (dp.empty()) dp = CfgStr(txt, "dumpPath");
    if (!dp.empty()) {
        
        if (dp[0] != '/') dp = g_exeDir + "/" + dp;
        g_dumpedPath = dp;
    }
    g_placeId = CfgU64(txt, "placeId", g_placeId);
    g_gameId = CfgU64(txt, "gameId", g_gameId);
    g_creatorId = CfgU64(txt, "creatorId", g_creatorId);
    g_userId = CfgU64(txt, "userId", g_userId);
    g_fHealth = CfgFlt(txt, "health", g_fHealth);
    g_fMaxHealth = CfgFlt(txt, "maxHealth", g_fMaxHealth);
    g_fWalk = CfgFlt(txt, "walkspeed", g_fWalk);
    g_fJumpPower = CfgFlt(txt, "jumpPower", g_fJumpPower);
    g_fJumpHeight = CfgFlt(txt, "jumpHeight", g_fJumpHeight);
    g_fHip = CfgFlt(txt, "hipHeight", g_fHip);
    g_fSlope = CfgFlt(txt, "maxSlopeAngle", g_fSlope);
    g_camX = CfgFlt(txt, "camPosX", g_camX);
    g_camY = CfgFlt(txt, "camPosY", g_camY);
    g_camZ = CfgFlt(txt, "camPosZ", g_camZ);
    g_camPitch = CfgFlt(txt, "camPitch", g_camPitch);
    g_fov = CfgFlt(txt, "fov", g_fov);
    g_minZoom = CfgFlt(txt, "minZoom", g_minZoom);
    g_maxZoom = CfgFlt(txt, "maxZoom", g_maxZoom);
    g_camMode = CfgInt(txt, "cameraMode", g_camMode);
    g_camType = CfgInt(txt, "cameraType", g_camType);
    g_winW = CfgInt(txt, "winW", g_winW);
    g_winH = CfgInt(txt, "winH", g_winH);
    g_teamColor = CfgInt(txt, "teamColor", g_teamColor);
    LogInfo("[config] place=%llu game=%llu creator=%llu user=%llu name=%s locale=%s", (unsigned long long)g_placeId, (unsigned long long)g_gameId, (unsigned long long)g_creatorId, (unsigned long long)g_userId, g_displayName.c_str(), g_localeId.c_str());
    return true;
}

bool LoadFpsXml() {
    
    const char* home = getenv("HOME");
    if (!home) return false;
    char path[512];
    snprintf(path, sizeof(path),
             "%s/.var/app/org.vinegarhq.Sober/data/sober/appData/"
             "GlobalBasicSettings_13.xml",
             home);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char* buf = nullptr;
    size_t cap = 0;
    ssize_t n;
    bool ok = false;
    while ((n = getline(&buf, &cap, f)) != -1) {
        const char* p = strstr(buf, "FramerateCap");
        if (!p) continue;
        const char* q = strchr(p, '>');
        if (q) {
            double v = atof(q + 1);
            if (v > 0) {
                g_maxFps = v;
                ok = true;
            }
        }
    }
    fclose(f);
    free(buf);
    return ok;
}
