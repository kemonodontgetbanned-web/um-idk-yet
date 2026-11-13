#include "dump.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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


static std::string SoberServerIp() {
    const char* home = getenv("HOME");
    if (!home) return "";
    std::string path = std::string(home) +
        "/.var/app/org.vinegarhq.Sober/data/sober/sober_logs/latest.log";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string data;
    char b[1 << 16];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) data.append(b, b + n);
    fclose(f);
    const char* key = "will connect to server ";
    const size_t kl = strlen(key);
    std::string ip;
    size_t pos = 0;
    while ((pos = data.find(key, pos)) != std::string::npos) {
        size_t s = pos + kl, e = s;
        while (e < data.size() &&
               ((data[e] >= '0' && data[e] <= '9') || data[e] == '.'))
            ++e;
        if (e > s) ip.assign(data, s, e - s);
        pos = e;
    }
    return ip;
}
bool DataModel() {
    uintptr_t pOff = ScanU64(g_dataModel, g_placeId, "PlaceId", "DataModel");
    uintptr_t gOff = ScanU64(g_dataModel, g_gameId, "GameId", "DataModel");
    uintptr_t cOff =
        ScanU64(g_dataModel, g_creatorId, "CreatorId", "DataModel");
    LogInfo("actual: place=%llu game=%llu creator=%llu (for config)",
            (unsigned long long)(pOff ? Read<uint64_t>(g_dataModel + pOff)
                                      : 0),
            (unsigned long long)(gOff ? Read<uint64_t>(g_dataModel + gOff)
                                      : 0),
            (unsigned long long)(cOff ? Read<uint64_t>(g_dataModel + cOff)
                                      : 0));
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    LogInfo("Workspace=%#lx", ws);
    if (!ws) return false;
    ScanU64(g_dataModel, ws, "Workspace", "DataModel");
    
    
    std::string ip = SoberServerIp();
    if (ip.empty()) {
        LogBad("MISS   %-28s %-14s (no endpoint in sober log)", "ServerIP",
               "DataModel");
    } else {
        char blk[0x1000];
        std::string key = ip + "|";
        uintptr_t at = 0;
        int count = 0;
        if (ReadBytes(g_dataModel, blk, sizeof(blk))) {
            for (size_t i = 0; i + key.size() < sizeof(blk); ++i) {
                if (memcmp(blk + i, key.c_str(), key.size()) != 0) continue;
                size_t e = i + key.size();
                while (e < sizeof(blk) && blk[e] >= 32 && blk[e] <= 126 &&
                       e - i < 64)
                    ++e;
                if (e < sizeof(blk) && blk[e] == 0) {
                    if (!count) at = i;
                    ++count;
                }
            }
        }
        if (count >= 1) {
            Emit("ServerIP", "DataModel", at, "string");
            LogInfo("ServerIP %.*s (%d candidates)", 40, blk + at, count);
        } else {
            LogBad("MISS   %-28s %-14s (want %s|...)", "ServerIP",
                   "DataModel", ip.c_str());
        }
    }
    
    {
        
        uintptr_t gj = FindFirstChild(ws, "GetAJob");
        if (gj) {
            auto ck = GetChildren(gj);
            if (!ck.empty()) {
                std::string jid = GetName(ck[0]);
                if (!jid.empty() && jid != "?")
                    ScanStr(g_dataModel, jid, "JobId", "DataModel", 0x1000);
                else
                    LogBad("MISS   %-28s %-14s (GetAJob kid unreadable)",
                           "JobId", "DataModel");
            }
        }
        uintptr_t sc = FindFirstChild(g_dataModel, "Script Context");
        if (sc) {
            uintptr_t holder = Read<uintptr_t>(g_dataModel + 0x78);
            uintptr_t kids = holder ? Read<uintptr_t>(holder) : 0;
            if (kids) ScanU64(kids, sc, "ScriptContext", "DataModel");
        }
        uintptr_t pv = FindFirstChild(ws, "PlaceVersion");
        if (pv) {
            auto ck = GetChildren(pv);
            if (!ck.empty()) {
                int ver = 0;
                if (sscanf(GetName(ck[0]).c_str(), "%d", &ver) == 1)
                    ScanI32(g_dataModel, ver, "PlaceVersion", "DataModel", 0,
                            0x1000);
            }
        }
        
        
        ScanI32(g_dataModel, 31, "GameLoaded", "DataModel", 0x500, 0x1000);
    }
    {
        
        uintptr_t rs = FindFirstChild(g_dataModel, "Run Service");
        if (rs) {
            for (uintptr_t jo = 0; jo < 0x400; jo += 1) {
                uintptr_t hb = Read<uintptr_t>(rs + jo);
                if (!IsValid(hb)) continue;
                for (uintptr_t i = 0xb0; i < 0xff; i += 1) {
                    double v = Read<double>(hb + i);
                    if (v == 0 || !std::isfinite(v)) continue;
                    double fps = 1.0 / v;
                    if (fabs(fps - g_maxFps) < 30.0) {
                        Emit("HeartbeatTask", "RunService", jo,
                             "unsigned __int64");
                        Emit("HeartbeatFPS", "RunService", i, "double");
                        jo = 0x400;  
                        break;
                    }
                }
            }
        }
    }
    return true;
}
}  
