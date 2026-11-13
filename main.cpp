#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "config.h"
#include "dump/dump.h"
#include "globals.h"
#include "mem.h"
#include "scanner.h"
#include "log.h"

int main(int argc, char** argv) {
    const char* cfg = "config.json";
    std::string step = "all";
    uintptr_t inspect = 0;
    bool luauOnly = false;
    bool flagsOnly = false;
    
    {
        char b[4096] = {};
        ssize_t n = readlink("/proc/self/exe", b, sizeof(b) - 1);
        std::string s(b, n > 0 ? n : 0);
        size_t sl = s.find_last_of('/');
        g_exeDir = (sl == std::string::npos) ? "." : s.substr(0, sl);
    }
    g_dumpedPath = g_exeDir + "/dump/libroblox.so";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--pid") && i + 1 < argc)
            g_pid = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--config") && i + 1 < argc)
            cfg = argv[++i];
        else if (!strcmp(argv[i], "--step") && i + 1 < argc)
            step = argv[++i];
        else if (!strcmp(argv[i], "--inspect") && i + 1 < argc)
            inspect = strtoull(argv[++i], nullptr, 16);
        else if (!strcmp(argv[i], "--luau"))
            luauOnly = true;
        else if (!strcmp(argv[i], "--flags"))
            flagsOnly = true;
    }
    
    
    if (luauOnly) {
        LogInfo("--luau: static Luau dump only");
        if (!Dump::Luau()) return 1;
        Dump::PublishLuau();
        return 0;
    }
    
    
    if (flagsOnly) {
        LogInfo("--flags: static fast-flag dump only");
        if (!Dump::Flags()) return 1;
        Dump::PublishFlags();
        return 0;
    }
    {
        
        bool cliCfg = false;
        for (int i = 1; i + 1 < argc; ++i)
            if (!strcmp(argv[i], "--config")) cliCfg = true;
        if (!cliCfg) {
            FILE* t = fopen(cfg, "r");
            if (t) {
                fclose(t);
            } else {
                static std::string alt;
                alt = g_exeDir + "/config.json";
                cfg = alt.c_str();
            }
        }
    }
    LoadConfig(cfg);
    if (LoadFpsXml())
        LogInfo("[fps] FramerateCap=%.0f (sober GlobalBasicSettings_13.xml)", g_maxFps);
    else
        LogInfo("[fps] xml not found, default %.0f", g_maxFps);
    if (g_pid < 0) {
        g_pid = FindMain();
        if (g_pid < 0) {
            LogBad("NO_GAME");
            return 1;
        }
        LogInfo("Main pid: %d", g_pid);
    }
    if (inspect) return Dump::Inspect(inspect) ? 0 : 1;
    if (step == "rttihist") return Dump::RttiHist() ? 0 : 1;
    if (step == "rttifind") return Dump::RttiFind() ? 0 : 1;
    if (step == "ugcfind") return Dump::UgcFind() ? 0 : 1;
    if (step == "finddm") return Dump::FindDm() ? 0 : 1;
    if (step == "findclass") return Dump::FindClass(argc, argv) ? 0 : 1;
    if (step == "finddesc") return Dump::FindDesc() ? 0 : 1;
    if (step == "dataslide") return Dump::DataSlide() ? 0 : 1;
    
    
    
    
    bool crit = true;
    bool ok = true;
    if (step == "all" || step == "static") crit &= Dump::Static();
    if (crit && (step == "all" || step == "general")) crit &= Dump::General();
    if (crit && (step == "all" || step == "datamodel"))
        crit &= Dump::DataModel();
    ok = crit;
    if (step != "all" && step != "static" && step != "general" &&
        step != "datamodel") {
        
        if (step == "players") ok &= Dump::Players();
        else if (step == "camera")
            ok &= Dump::Camera();
        else if (step == "humanoid")
            ok &= Dump::Humanoid();
        else if (step == "parts")
            ok &= Dump::Parts();
        else if (step == "world")
            ok &= Dump::World();
        else if (step == "mouse")
            ok &= Dump::Mouse();
        else if (step == "workspace")
            ok &= Dump::Workspace();
        else if (step == "animation")
            ok &= Dump::Animation();
        else if (step == "lighting")
            ok &= Dump::Lighting();
        else if (step == "interactables")
            ok &= Dump::Interactables();
    } else if (step == "all" && crit) {
        if (!Dump::Players()) LogBad("players FAILED (soft)");
        if (!Dump::Camera()) LogBad("camera FAILED (soft)");
        if (!Dump::Humanoid()) LogBad("humanoid FAILED (soft)");
        if (!Dump::Parts()) LogBad("parts FAILED (soft)");
        if (!Dump::World()) LogBad("world FAILED (soft)");
        if (!Dump::Mouse()) LogBad("mouse FAILED (soft)");
        if (!Dump::Workspace()) LogBad("workspace FAILED (soft)");
        if (!Dump::Animation()) LogBad("animation FAILED (soft)");
        if (!Dump::Lighting()) LogBad("lighting FAILED (soft)");
        if (!Dump::Interactables()) LogBad("interactables FAILED (soft)");
    }
    if (ok && crit)
        LogOk("FLOW OK");
    else
        LogBad("FLOW FAILED");
    if (ok && crit && (step == "all")) WriteHpp();
    if (ok && crit && (step == "all")) WriteJson();
    if (ok && crit && (step == "all")) WriteStruct();
    if (ok && crit && (step == "all")) Publish();
    return (ok && crit) ? 0 : 1;
}
