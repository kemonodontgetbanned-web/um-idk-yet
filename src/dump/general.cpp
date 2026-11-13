#include "dump.h"

#include <algorithm>
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
bool DiscoverName(uintptr_t& ncOut, uintptr_t& nOffOut) {
    for (uintptr_t i = 0; i < 0x200; i += 1) {
        uintptr_t one = Read<uintptr_t>(g_dataModel + i);
        if (!IsValid(one)) continue;
        char buf[64];
        if (!ReadBytes(one, buf, sizeof(buf))) continue;
        for (size_t k = 0; k + 3 < sizeof(buf); ++k) {
            if (memcmp(buf + k, "Ugc", 3) != 0 || buf[k + 3] != 0) continue;
            for (int j = (int)k - 1; j <= (int)k; ++j) {
                if (j < 0) continue;
                ncOut = i;
                nOffOut = (uintptr_t)j;
                return true;
            }
        }
    }
    return false;
}

bool DiscoverChildren() {
    
    
    
    for (uintptr_t stride : {16u, 8u}) {
        for (uintptr_t i = 0; i < 0x1000; i += 1) {
            uintptr_t holder = Read<uintptr_t>(g_dataModel + i);
            if (!IsValid(holder)) continue;
            uintptr_t start = Read<uintptr_t>(holder);
            if (!IsValid(start)) continue;
            for (int k = 0; k < 100; k += 1) {
                uintptr_t child = Read<uintptr_t>(start + k * stride);
                if (!IsValid(child)) continue;
                std::string nm = GetName(child);
                if (nm == "Workspace" || nm == "Players" ||
                    nm == "Run Service") {
                    g_childrenStart = i;
                    g_childStride = stride;
                    LogInfo("ChildrenStart=0x%lx stride=%lu (via %s)", (unsigned long)i, (unsigned long)stride, nm.c_str());
                    return true;
                }
            }
        }
    }
    return false;
}

bool General() {
    uintptr_t nc = 0, noff = 0;
    if (DiscoverName(nc, noff)) {
        g_nameContainer = nc;
        g_nameOff = noff;
        LogInfo("discovered NameContainer=0x%lx Name=0x%lx", (unsigned long)nc, (unsigned long)noff);
    } else {
        g_nameContainer = 0x70;
        g_nameOff = 8;
        LogInfo("FALLBACK NameContainer=0x70 Name=8");
    }
    if (!DiscoverChildren()) {
        g_childrenStart = 0x78;
        LogInfo("FALLBACK ChildrenStart=0x78");
    }
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    if (!ws) {
        LogBad("General FAILED: no Workspace kid");
        return false;
    }
    Emit("NameContainer", "Instance", g_nameContainer, "unsigned __int64");
    Emit("Name", "Instance", g_nameOff, "string");
    Emit("ChildrenStart", "Instance", g_childrenStart, "unsigned __int64");
    Emit("ChildrenEnd", "Instance", 0x8, "unsigned __int64");
    ScanU64(ws, g_dataModel, "Parent", "Instance");
    
    uintptr_t cdOff = 0;
    for (uintptr_t off = 0; off < 0x200 && !cdOff; off += 1) {
        uintptr_t d = Read<uintptr_t>(g_dataModel + off);
        if (!IsValid(d)) continue;
        uintptr_t np = Read<uintptr_t>(d + 8);
        if (!IsValid(np)) continue;
        char b[40];
        if (!ReadBytes(np, b, sizeof(b))) continue;
        for (int sk = 0; sk < 3; ++sk) {
            int n = 0;
            while (n < 36 && b[sk + n] != 0) {
                if (b[sk + n] < 32 || b[sk + n] > 126) break;
                ++n;
            }
            if (n > 0 && b[sk + n] == 0 &&
                std::string(b + sk, n) == "DataModel") {
                cdOff = off;
                break;
            }
        }
    }
    if (!cdOff) {
        LogInfo("General PARTIAL: no ClassDescriptor");
        return true;
    }
    Emit("ClassDescriptor", "Instance", cdOff, "unsigned __int64");
    Emit("ClassName", "Instance", 8, "string");
    LogInfo("ClassDescriptor=0x%lx", (unsigned long)cdOff);
    
    
    for (uintptr_t bo = 0x150; bo < 0x400; bo += 1) {
        uintptr_t cd = Read<uintptr_t>(ws + cdOff);
        if (!IsValid(cd)) break;
        uintptr_t bc = Read<uintptr_t>(cd + bo);
        if (!IsValid(bc)) continue;
        uintptr_t np = Read<uintptr_t>(bc + 8);
        if (!IsValid(np)) continue;
        char b[40];
        if (!ReadBytes(np, b, sizeof(b))) continue;
        std::string nm;
        for (int sk = 0; sk < 3 && nm.empty(); ++sk) {
            int n = 0;
            while (n < 36 && b[sk + n] != 0) {
                if (b[sk + n] < 32 || b[sk + n] > 126) break;
                ++n;
            }
            if (n > 0 && b[sk + n] == 0) nm.assign(b + sk, n);
        }
        if (nm == "WorldRoot") {
            Emit("ClassBase", "Instance", bo, "unsigned __int64");
            break;
        }
        if (bo == 0x3FF)
            LogInfo("General PARTIAL: no ClassBase");
    }
    {
        
        uintptr_t bha = FindFirstChild(ws, "BoxHandleAdornment");
        uintptr_t pos = FindFirstChild(ws, "Position");
        if (bha && pos) ScanU64(bha, pos, "Adornee", "Misc");
    }
    
    
    
    return true;
}
}  
