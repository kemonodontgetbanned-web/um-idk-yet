#include "dump.h"

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
bool Animation() {
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    if (!ws) {
        LogBad("Animation FAILED: no Workspace kid");
        return false;
    }
    uintptr_t anim = FindFirstChild(ws, "Animation");
    if (anim)
        ScanStr(anim, "rbxassetid://121903298942078", "AnimationId", "Misc",
                0x2000);
    
    
    uintptr_t valInst = FindFirstChild(ws, "Value");
    uintptr_t valueOff = 0;
    if (valInst) valueOff = ScanStr(valInst, "Value :3", "Value", "Misc",
                                    0x1000);
    if (!valueOff) {
        LogBad("Animation PARTIAL: no Misc:Value (tracks skipped)");
        return true;
    }
    uintptr_t tr[4] = {0};
    const char* hn[4] = {"AnimationTrackHolder", "AnimationTrackHolder2",
                         "AnimationTrackHolder3", "AnimationTrackHolder4"};
    for (int k = 0; k < 4; ++k) {
        uintptr_t h = FindFirstChild(ws, hn[k]);
        if (h) tr[k] = Read<uintptr_t>(h + valueOff);
    }
    if (!tr[0] || !IsValid(tr[0])) {
        LogBad("Animation PARTIAL: no track0");
        return true;
    }
    uintptr_t rig = FindFirstChild(ws, "Rig");
    uintptr_t rhum = rig ? FindFirstChild(rig, "Humanoid") : 0;
    uintptr_t ator = rhum ? FindFirstChild(rhum, "Animator") : 0;
    ScanU64(tr[0], anim, "Animation", "AnimationTrack");
    if (ator) ScanU64(tr[0], ator, "Animator", "AnimationTrack");
    uintptr_t spd = ScanFlt(tr[0], 6.218f, "Speed", "AnimationTrack");
    if (spd) Emit("TimePosition", "AnimationTrack", spd + 4, "float");
    {
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        int want[4] = {1, 0, 0, 1};
        for (int k = 0; k < 4; ++k)
            if (tr[k] && IsValid(tr[k])) {
                is.push_back(tr[k]);
                vs.push_back(want[k]);
            }
        if (!is.empty()) ScanBoolM(is, vs, "Looped", "AnimationTrack");
    }
    {
        std::vector<uintptr_t> is;
        std::vector<int> vs;
        int want[4] = {1, 0, 0, 1};
        for (int k = 0; k < 4; ++k)
            if (tr[k] && IsValid(tr[k])) {
                is.push_back(tr[k]);
                vs.push_back(want[k]);
            }
        if (!is.empty())
            ScanBoolM(is, vs, "IsPlaying", "AnimationTrack", 0x200, 0x1000);
    }
    if (ator) {
        
        
        for (uintptr_t i = 0x600; i < 0x1000; i += 1) {
            uintptr_t head = Read<uintptr_t>(ator + i);
            if (!IsValid(head)) continue;
            uintptr_t node = Read<uintptr_t>(head);
            size_t guard = 0;
            bool found = false;
            while (node && node != head && guard++ < 4096) {
                uintptr_t t = Read<uintptr_t>(node + 0x10);
                if (IsValid(t) && GetName(t) == "Animation") {
                    Emit("ActiveAnimations", "Animator", i,
                         "unsigned __int64");
                    found = true;
                    break;
                }
                node = Read<uintptr_t>(node);
            }
            if (found) break;
            if (i == 0xFFF)
                LogBad("MISS   %-28s %-14s", "ActiveAnimations", "Animator");
        }
    }
    return true;
}
}  
