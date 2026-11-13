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
bool Interactables() {
    uintptr_t ws = FindFirstChild(g_dataModel, "Workspace");
    if (!ws) {
        LogBad("Interactables FAILED: no Workspace kid");
        return false;
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
    uintptr_t pp = FindFirstChild(ws, "ProximityPrompt");
    if (pp) {
        ScanStr(pp, "This is action text", "ActionText", "ProximityPrompt",
                0x1000);
        ScanStr(pp, "This is object text", "ObjectText", "ProximityPrompt",
                0x1000);
        ScanFlt(pp, 0.282f, "HoldDuration", "ProximityPrompt", 0, 0x1000,
                0.01f);
        ScanFlt(pp, 10.882f, "MaxActivationDistance", "ProximityPrompt", 0,
                0x1000, 0.01f);
        ScanI32(pp, 101, "KeyCode", "ProximityPrompt", 0, 0x1000);
        ScanI32(pp, 1000, "GamepadKeyCode", "ProximityPrompt", 0, 0x1000);
    }
    {
        uintptr_t pp2 = FindFirstChild(ws, "ProximityPrompt2");
        uintptr_t pp3 = FindFirstChild(ws, "ProximityPrompt3");
        auto p = pickB({pp, pp2, pp3}, {1, 0, 1});
        if (!p.first.empty())
            ScanBoolM(p.first, p.second, "Enabled", "ProximityPrompt");
        auto q = pickB({pp, pp2, pp3}, {1, 1, 0});
        if (!q.first.empty())
            ScanBoolM(q.first, q.second, "RequiresLineOfSight",
                      "ProximityPrompt");
    }
    uintptr_t cd = FindFirstChild(ws, "ClickDetector");
    if (cd) {
        ScanFlt(cd, 32.211f, "MaxActivationDistance", "ClickDetector", 0,
                0x1000, 0.01f);
        ScanStr(cd, "rbxassetid://73879578225090", "MouseIcon",
                "ClickDetector", 0x2000);
    }
    uintptr_t dd = FindFirstChild(ws, "DragDetector");
    if (dd) {
        uintptr_t ddp = FindFirstChild(ws, "dragdetectpart");
        if (ddp) ScanU64(dd, ddp, "ReferenceInstance", "DragDetector");
        ScanFlt(dd, 743.321f, "MaxActivationDistance", "DragDetector");
        ScanFlt(dd, 120.83f, "MaxDragAngle", "DragDetector");
        ScanVec3(dd, 128.83f, 129.83f, 130.83f, "MaxDragTranslation",
                 "DragDetector");
        ScanFlt(dd, 4.842f, "MinDragAngle", "DragDetector");
        ScanVec3(dd, 118.83f, 119.83f, 110.83f, "MinDragTranslation",
                 "DragDetector");
        ScanStr(dd, "rbxassetid://73879578225091", "ActivatedCursorIcon",
                "DragDetector", 0x2000);
        ScanStr(dd, "rbxassetid://73879578225090", "CursorIcon",
                "DragDetector", 0x2000);
        ScanFlt(dd, 7452.386f, "MaxForce", "DragDetector");
        ScanFlt(dd, 236.872f, "MaxTorque", "DragDetector");
        ScanFlt(dd, 881.415f, "Responsiveness", "DragDetector");
    }
    return true;
}
}  
