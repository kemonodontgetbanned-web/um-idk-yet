#include "dump.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "globals.h"
#include "log.h"
#include "mem.h"
#include "scanner.h"












namespace Dump {
namespace {

static std::vector<char> g_file;

static bool LoadFile() {
    FILE* f = fopen(g_dumpedPath.c_str(), "rb");
    if (!f) {
        LogBad("FFlags FAILED: no reference binary");
        return false;
    }
    char b[1 << 20];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) g_file.insert(g_file.end(), b, b + n);
    fclose(f);
    return true;
}

static inline unsigned char U8(size_t i) { return static_cast<unsigned char>(g_file[i]); }
static inline bool Have(size_t i, size_t n) { return i + n <= g_file.size(); }


static std::vector<uint64_t> FindStrings(const char* name) {
    std::vector<uint64_t> out;
    size_t nl = strlen(name);
    size_t off = 0;
    while (off + nl + 1 <= g_file.size()) {
        const void* p = memmem(g_file.data() + off, g_file.size() - off, name, nl);
        if (!p) break;
        size_t s = (const char*)p - g_file.data();
        off = s + 1;
        if (g_file[s + nl] != 0) continue;
        if (s > 0) {
            unsigned char pre = g_file[s - 1];
            if ((pre >= 'A' && pre <= 'Z') || (pre >= 'a' && pre <= 'z') ||
                (pre >= '0' && pre <= '9') || pre == '_')
                continue;
        }
        out.push_back(s);
        if (out.size() >= 8) break;
    }
    return out;
}


struct RipRef {
    size_t pos;
    uint64_t target;
    int dst;  
};
static std::vector<RipRef> RipRefs(uint64_t strVA) {
    std::vector<RipRef> out;
    for (size_t i = 0; i + 7 < g_file.size(); ++i) {
        size_t q = i;
        uint8_t rex = 0;
        if (U8(q) >= 0x48 && U8(q) <= 0x4F) rex = U8(q++);
        if (q + 6 > g_file.size()) continue;
        uint8_t op = U8(q);
        if (op != 0x8D && op != 0x8B) continue;
        unsigned char mr = U8(q + 1);
        if (((mr >> 6) & 3) != 0 || (mr & 7) != 5) continue;  
        int32_t disp;
        memcpy(&disp, &g_file[q + 2], 4);
        uint64_t t = q + 6 + (int64_t)disp;
        if (t != strVA) continue;
        int dst = ((mr >> 3) & 7) + ((rex & 4) ? 8 : 0);
        out.push_back({i, t, dst});
        i = q + 5;
    }
    return out;
}


static uint64_t RipStore(size_t pos, int wantReg) {
    if (!Have(pos, 7)) return 0;
    size_t q = pos;
    uint8_t rex = 0;
    if (U8(q) >= 0x48 && U8(q) <= 0x4F) rex = U8(q++);
    if (q + 6 > g_file.size() || U8(q) != 0x89) return 0;
    unsigned char mr = U8(q + 1);
    if (((mr >> 6) & 3) != 0 || (mr & 7) != 5) return 0;
    int src = ((mr >> 3) & 7) + ((rex & 4) ? 8 : 0);
    if (wantReg >= 0 && src != wantReg) return 0;
    int32_t disp;
    memcpy(&disp, &g_file[q + 2], 4);
    return q + 6 + (int64_t)disp;
}

struct Want {
    const char* bin;   
    const char* emit;  
};

static bool DumpOneAt(const Want& w, const char* name, bool loud) {
    auto strs = FindStrings(name);
    if (strs.empty()) {
        if (loud) LogBad("MISS   FFlag %s (no string %s)", w.emit, name);
        return false;
    }
    bool anyRef = false;
    for (auto strVA : strs) {
        auto refs = RipRefs(strVA);
        if (refs.empty()) continue;
        anyRef = true;
        for (auto& r : refs) {
        
        for (size_t i = r.pos; i + 7 < r.pos + 48 && i + 7 < g_file.size(); ++i) {
            uint64_t nameSlot = RipStore(i, r.dst);
            if (!nameSlot) continue;
            uint64_t valSlot = nameSlot - 8;
            
            const char* width = nullptr;
            uint64_t dflt = 0;
            bool hasDflt = false;
            size_t lo = r.pos > 64 ? r.pos - 64 : 0;
            for (size_t k = lo; k + 10 < r.pos + 64 && k + 10 < g_file.size(); ++k) {
                size_t q = k;
                bool rexW = false;
                if (U8(q) >= 0x40 && U8(q) <= 0x4F) {
                    rexW = (U8(q) & 8) != 0;  
                    ++q;                      
                }
                if (q + 9 >= g_file.size()) continue;
                
                if (U8(q) == 0xC7 && U8(q + 1) == 0x05) {
                    int32_t dp, im;
                    memcpy(&dp, &g_file[q + 2], 4);
                    memcpy(&im, &g_file[q + 6], 4);
                    if ((uint64_t)(q + 10 + (int64_t)dp) == valSlot) {
                        width = "uint32_t";
                        dflt = (uint32_t)im;
                        hasDflt = true;
                    }
                }
                
                
                
                if (!rexW && U8(q) == 0x89 && ((U8(q + 1) >> 6) & 3) == 0 &&
                    (U8(q + 1) & 7) == 5) {
                    int32_t dp;
                    memcpy(&dp, &g_file[q + 2], 4);
                    if ((uint64_t)(q + 6 + (int64_t)dp) == valSlot) width = "uint32_t";
                }
                
                if (U8(q) == 0xC6) {
                    unsigned char mr = U8(q + 1);
                    if (((mr >> 6) & 3) == 0 && (mr & 7) == 5) {
                        int32_t dp;
                        memcpy(&dp, &g_file[q + 2], 4);
                        if ((uint64_t)(q + 7 + (int64_t)dp) == valSlot) {
                            width = "uint8_t";
                            dflt = U8(q + 6);
                            hasDflt = true;
                        }
                    }
                }
                
                
                if (U8(q) == 0x88) {
                    unsigned char mr = U8(q + 1);
                    if (((mr >> 6) & 3) == 0 && (mr & 7) == 5) {
                        int32_t dp;
                        memcpy(&dp, &g_file[q + 2], 4);
                        if ((uint64_t)(q + 6 + (int64_t)dp) == valSlot) width = "uint8_t";
                    }
                }
                if (width) break;
            }
            if (!width) {
                if (loud)
                    LogInfo("FFlag %s: name @%#lx but no value store at %#lx; trying next site",
                            w.emit, (unsigned long)nameSlot, (unsigned long)valSlot);
                continue;
            }
            LogOk("FFlag %-32s slot %#lx [%s]%s", w.emit, (unsigned long)valSlot, width,
                  hasDflt ? "" : " (default unread)");
            if (hasDflt) LogInfo("  default=%llu", (unsigned long long)dflt);
            Emit(w.emit, "FFlags", valSlot, "unsigned __int64");
            return true;
        }
    }
    }
    if (loud) {
        if (!anyRef)
            LogBad("MISS   FFlag %s (no .text refs)", w.emit);
        else
            LogBad("MISS   FFlag %s (no name store near refs)", w.emit);
    }
    return false;
}




static bool DumpOne(const Want& w) {
    if (DumpOneAt(w, w.bin, false)) return true;
    if (strcmp(w.emit, w.bin) != 0 && DumpOneAt(w, w.emit, false)) return true;
    DumpOneAt(w, w.bin, true);  
    return false;
}

}  


namespace {

static std::string FileMd5(const char* path) {
    std::string cmd = std::string("md5sum '") + path + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "unknown";
    char b[64] = {};
    size_t n = fread(b, 1, sizeof(b) - 1, p);
    pclose(p);
    std::string s(b, n);
    if (s.size() >= 32 && s.substr(0, 32).find_first_not_of("0123456789abcdef") == std::string::npos)
        return s.substr(0, 32);
    return "unknown";
}

static std::string ApkVersion() {
    const char* home = getenv("HOME");
    if (!home) return "unknown";
    std::string apk = std::string(home) +
                      "/.var/app/org.vinegarhq.Sober/data/sober/packages/"
                      "x86_64/com.roblox.client/base.apk";
    FILE* t = fopen(apk.c_str(), "r");
    if (!t) return "unknown";
    fclose(t);
    std::string out;
    FILE* p = popen(("aapt dump badging \"" + apk + "\" 2>/dev/null | grep versionName").c_str(),
                    "r");
    if (!p) return "unknown";
    char b[512];
    while (fgets(b, sizeof(b), p)) out += b;
    pclose(p);
    size_t q = out.find("versionName='");
    if (q == std::string::npos) return "unknown";
    q += 13;
    size_t e = out.find('\'', q);
    if (e == std::string::npos || e <= q) return "unknown";
    return out.substr(q, e - q);
}

void WriteFFlagsHpp() {
    std::string md5 = FileMd5(g_dumpedPath.c_str());
    std::string apkVer = ApkVersion();
    std::string base = g_dumpedPath;
    size_t sl = base.find_last_of('/');
    if (sl != std::string::npos) base = base.substr(sl + 1);
    
    std::string prevMd5;
    {
        FILE* o = fopen("fflags_offsets.hpp", "r");
        if (o) {
            char ln[512];
            while (fgets(ln, sizeof(ln), o)) {
                std::string s = ln;
                size_t p = s.find("(md5 ");
                if (p != std::string::npos && s.size() >= p + 5 + 32) {
                    std::string m = s.substr(p + 5, 32);
                    if (m.find_first_not_of("0123456789abcdef") == std::string::npos)
                        prevMd5 = m;
                    break;
                }
            }
            fclose(o);
        }
    }
    if (!prevMd5.empty() && prevMd5 != md5) {
        LogBad("fflags version changed (md5 %s -> %s): NOT carrying old offsets",
               prevMd5.c_str(), md5.c_str());
    } else {
        FILE* o = fopen("fflags_offsets.hpp", "r");
        if (o) {
            char ln[512];
            std::string cat;
            int kept = 0;
            struct KE {
                std::string cat, name;
                uintptr_t val;
            };
            std::vector<KE> old;
            while (fgets(ln, sizeof(ln), o)) {
                std::string s = ln;
                size_t np = s.find("namespace ");
                if (np != std::string::npos) {
                    size_t e = s.find_first_of(" {", np + 10);
                    cat = s.substr(np + 10, e - np - 10);
                    continue;
                }
                unsigned long v = 0;
                char nm[128] = {};
                if (sscanf(s.c_str(), " inline constexpr uintptr_t %127s = 0x%lx", nm, &v) == 2) {
                    std::string n = nm;
                    old.push_back({cat, n, (uintptr_t)v});
                }
            }
            fclose(o);
            for (auto& e : old) {
                bool have = false;
                for (auto& g : g_emitted)
                    if (g.cat == e.cat && g.name == e.name) {
                        have = true;
                        break;
                    }
                if (!have) {
                    g_emitted.push_back({e.cat, e.name, "kept-old", e.val});
                    LogInfo("KEPT-OLD %-28s %-14s 0x%lx", e.name.c_str(), e.cat.c_str(),
                            (unsigned long)e.val);
                    ++kept;
                }
            }
            if (kept) LogInfo("KEPT-OLD total %d offsets from previous header", kept);
        }
    }
    FILE* f = fopen("fflags_offsets.hpp", "w");
    if (!f) {
        LogBad("could not write fflags_offsets.hpp");
        return;
    }
    
    fprintf(f, "//dumped by idom - ix dumper v2.3\n");
    fprintf(f, "//build: %s (md5 %s)\n", base.c_str(), md5.c_str());
    fprintf(f, "//apk version: %s\n", apkVer.c_str());
    fprintf(f, "//discord: _idom_\n");
    fprintf(f, "//offsets found: %zu\n", g_emitted.size());
    fprintf(f, "#pragma once\n\n#include <cstdint>\n\nnamespace Offsets {\n");
    std::vector<std::string> order;
    for (auto& e : g_emitted)
        if (std::find(order.begin(), order.end(), e.cat) == order.end()) order.push_back(e.cat);
    for (size_t ci = 0; ci < order.size(); ++ci) {
        fprintf(f, "    namespace %s {\n", order[ci].c_str());
        for (auto& e : g_emitted) {
            if (e.cat != order[ci]) continue;
            fprintf(f, "        inline constexpr uintptr_t %s = 0x%lx;\n", e.name.c_str(),
                    (unsigned long)e.val);
        }
        fprintf(f, "    }%s", ci + 1 < order.size() ? "\n\n" : "\n");
    }
    fprintf(f, "}\n");
    fclose(f);
    {
        char cwd[4096] = {};
        if (!getcwd(cwd, sizeof(cwd) - 1)) cwd[0] = 0;
        LogOk("wrote %s/fflags_offsets.hpp (%zu offsets)", cwd, g_emitted.size());
    }
}

static bool CopyFileFlags(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    out << in.rdbuf();
    out.flush();
    return (bool)out;
}

}  





void PublishFlags() {
    const char* noPub = getenv("IX_NO_PUBLISH");
    const char* home = getenv("HOME");
    if (noPub && noPub[0]) {
        LogInfo("[publish:fflags] skipped (IX_NO_PUBLISH set)");
        return;
    }
    if (!home) {
        LogBad("[publish:fflags] skipped (no HOME)");
        return;
    }
    std::string md5 = FileMd5(g_dumpedPath.c_str());
    std::string apkVer = ApkVersion();
    std::string upDir = std::string(home) + "/offsets_upload";
    char cwd2[4096] = {};
    std::string cwd = getcwd(cwd2, sizeof(cwd2) - 1) ? std::string(cwd2) : ".";
    bool ok = true;
    if (!CopyFileFlags(cwd + "/fflags_offsets.hpp", upDir + "/fflags_offsets.hpp")) {
        LogBad("[publish:fflags] copy failed (fflags_offsets.hpp)");
        ok = false;
    }
    std::string fflagsPath;
    if (ok && apkVer != "unknown") {
        std::string vdir = upDir + "/version/" + apkVer;
        if (system(("mkdir -p \"" + vdir + "\"").c_str()) != 0) {
            LogBad("[publish:fflags] mkdir failed (%s)", vdir.c_str());
            ok = false;
        } else if (!CopyFileFlags(cwd + "/fflags_offsets.hpp", vdir + "/fflags_offsets.hpp")) {
            LogBad("[publish:fflags] archive copy failed (%s)", vdir.c_str());
            ok = false;
        } else {
            fflagsPath = "version/" + apkVer + "/fflags_offsets.hpp";
            LogOk("[publish:fflags] archived %s", (upDir + "/" + fflagsPath).c_str());
        }
    } else if (ok) {
        LogBad("[publish:fflags] unknown apk: root copy only, no archive/versions entry");
    }
    if (ok && !fflagsPath.empty()) {
        std::string vpath = upDir + "/versions";
        struct VEnt {
            std::string apk, md5, path, st, luau, ff;
            size_t count = 0;
        };
        std::vector<VEnt> ents;
        std::vector<std::string> other;
        {
            FILE* vf = fopen(vpath.c_str(), "r");
            if (vf) {
                char ln[512];
                std::string blk;
                auto flush = [&]() {
                    if (blk.empty()) return;
                    VEnt e;
                    char b1[64] = {}, b2[64] = {}, b3[256] = {}, b4[256] = {}, b5[256] = {},
                         b6[256] = {};
                    auto grab = [&](const char* key, char* dst, size_t n) {
                        std::string k = std::string("\"") + key + "\"";
                        size_t p = blk.find(k);
                        if (p == std::string::npos) return false;
                        p = blk.find('"', p + k.size());
                        if (p == std::string::npos) return false;
                        size_t q = blk.find('"', p + 1);
                        if (q == std::string::npos) return false;
                        snprintf(dst, n, "%s", blk.substr(p + 1, q - p - 1).c_str());
                        return true;
                    };
                    bool hasApk = grab("apk", b1, sizeof(b1));
                    grab("md5", b2, sizeof(b2));
                    grab("path", b3, sizeof(b3));
                    grab("struct", b4, sizeof(b4));
                    grab("luau", b5, sizeof(b5));
                    grab("fflags", b6, sizeof(b6));
                    size_t cnt = 0;
                    size_t pc2 = blk.find("\"offsets\"");
                    if (pc2 != std::string::npos) {
                        size_t cc = blk.find(':', pc2);
                        if (cc != std::string::npos) cnt = strtoul(blk.c_str() + cc + 1, nullptr, 10);
                    }
                    if (hasApk) {
                        e.apk = b1;
                        e.md5 = b2;
                        e.path = b3;
                        e.st = b4;
                        e.luau = b5;
                        e.ff = b6;
                        e.count = cnt;
                        ents.push_back(e);
                    } else if (blk.find('[') == std::string::npos &&
                               blk.find(']') == std::string::npos) {
                        other.push_back(blk);
                    }
                    blk.clear();
                };
                while (fgets(ln, sizeof(ln), vf)) {
                    std::string s = ln;
                    if (s.find('{') != std::string::npos && !blk.empty()) flush();
                    if (s.find('{') != std::string::npos || !blk.empty() || s.find('}') != std::string::npos)
                        blk += s;
                    if (s.find('}') != std::string::npos) flush();
                }
                flush();
                fclose(vf);
            }
        }
        bool attached = false;
        for (auto& e : ents) {
            if (e.apk == apkVer || (!md5.empty() && e.md5 == md5 && md5 != "unknown")) {
                e.ff = fflagsPath;
                attached = true;
                break;
            }
        }
        if (!attached) {
            VEnt cur{apkVer, md5, "", "", "", fflagsPath, g_emitted.size()};
            ents.insert(ents.begin(), cur);
            LogBad("[publish:fflags] no versions entry for apk %s: fflags-only placeholder "
                   "(run --step all to fill path/offsets)",
                   apkVer.c_str());
        }
        {
            FILE* vf = fopen(vpath.c_str(), "w");
            if (!vf) {
                LogBad("[publish:fflags] cannot write versions index");
                ok = false;
            } else {
                fprintf(vf, "[\n");
                for (size_t i = 0; i < ents.size(); ++i) {
                    auto& e = ents[i];
                    fprintf(vf, "  {\n");
                    fprintf(vf, "    \"apk\": \"%s\",\n", e.apk.c_str());
                    fprintf(vf, "    \"md5\": \"%s\",\n", e.md5.c_str());
                    fprintf(vf, "    \"offsets\": %zu,\n", e.count);
                    bool more1 = !e.st.empty() || !e.luau.empty() || !e.ff.empty();
                    fprintf(vf, "    \"path\": \"%s\"%s\n", e.path.c_str(), more1 ? "," : "");
                    bool more2 = !e.luau.empty() || !e.ff.empty();
                    if (!e.st.empty())
                        fprintf(vf, "    \"struct\": \"%s\"%s\n", e.st.c_str(), more2 ? "," : "");
                    bool more3 = !e.ff.empty();
                    if (!e.luau.empty())
                        fprintf(vf, "    \"luau\": \"%s\"%s\n", e.luau.c_str(), more3 ? "," : "");
                    if (!e.ff.empty()) fprintf(vf, "    \"fflags\": \"%s\"\n", e.ff.c_str());
                    fprintf(vf, "  }%s\n", i + 1 < ents.size() ? "," : "");
                }
                for (auto& o : other) fputs(o.c_str(), vf);
                fprintf(vf, "]\n");
                fclose(vf);
                LogOk("[publish:fflags] versions index updated (%zu entries)", ents.size());
            }
        }
    }
    if (!ok) return;
    char msg[160];
    snprintf(msg, sizeof(msg), "Update fflags offsets (%zu offsets, apk %s)", g_emitted.size(),
             apkVer.c_str());
    std::string files = "fflags_offsets.hpp versions";
    if (!fflagsPath.empty()) files += " " + fflagsPath;
    std::string cmd = "git -C \"" + upDir + "\" diff --quiet -- " + files +
                      " 2>/dev/null || (git -C \"" + upDir + "\" add " + files + " && git -C \"" +
                      upDir + "\" commit -qm \"" + msg + "\" && GIT_TERMINAL_PROMPT=0 git -C \"" +
                      upDir + "\" push 2>&1)";
    int rc = system(cmd.c_str());
    if (rc == 0)
        LogOk("[publish:fflags] website up to date / pushed");
    else
        LogBad("[publish:fflags] git push failed (rc=%d)", rc);
}

bool Flags() {
    if (!LoadFile()) return false;
    LogInfo("fflags static on %s (%zu bytes)", g_dumpedPath.c_str(), g_file.size());
    
    
    
    
    const Want wants[] = {
        {"TaskSchedulerTargetFps", "DFIntTaskSchedulerTargetFps"},
        {"DebugForceMSAASamples", "DebugForceMSAASamples"},
        {"DebugRunParallelLuaOnMainThread", "DebugRunParallelLuaOnMainThread"},
        {"TextureQualityOverrideEnabled", "DFFlagTextureQualityOverrideEnabled"},
        {"TextureQualityOverride", "DFIntTextureQualityOverride"},
        {"TextureCompositorLowResFactor", "FIntTextureCompositorLowResFactor"},
        {"CSGLevelOfDetailSwitchingDistance", "DFIntCSGLevelOfDetailSwitchingDistance"},
        {"CSGLevelOfDetailSwitchingDistanceL12", "DFIntCSGLevelOfDetailSwitchingDistanceL12"},
        {"CSGLevelOfDetailSwitchingDistanceL23", "DFIntCSGLevelOfDetailSwitchingDistanceL23"},
        {"CSGLevelOfDetailSwitchingDistanceL34", "DFIntCSGLevelOfDetailSwitchingDistanceL34"},
        {"FRMMaxGrassDistance", "FIntFRMMaxGrassDistance"},
        {"FRMMinGrassDistance", "FIntFRMMinGrassDistance"},
        {"DebugPauseVoxelizer", "DFFlagDebugPauseVoxelizer"},
        {"CSGVoxelizerFadeRadius", "FIntCSGVoxelizerFadeRadius"},
        {"TerrainArraySliceSize", "FIntTerrainArraySliceSize"},
        {"DebugFRMQualityLevelOverride", "DFIntDebugFRMQualityLevelOverride"},
        {"DebugRestrictGCDistance", "DFIntDebugRestrictGCDistance"},
        {"DebugTextureManagerSkipMips", "FIntDebugTextureManagerSkipMips"},
        {"EnableQuickGameLaunch", "FFlagEnableQuickGameLaunch"},
        {"DebugDontRenderScreenGui", "FFlagDebugDontRenderScreenGui"},
        {"DebugSSAOForce", "FFlagDebugSSAOForce"},
        {"SSAOMipLevels", "FIntSSAOMipLevels"},
        {"SSAO", "FIntSSAO"},
        {"RobloxGuiBlurIntensity", "FIntRobloxGuiBlurIntensity"},
        {"AnimationLodFacsDistanceMin", "DFIntAnimationLodFacsDistanceMin"},
        {"AnimationLodFacsDistanceMax", "DFIntAnimationLodFacsDistanceMax"},
        {"AnimationLodFacsVisibilityDenominator", "DFIntAnimationLodFacsVisibilityDenominator"},
        {"DebugSkyGray", "FFlagDebugSkyGray"},
        {"TextureCompositorActiveJobs", "DFIntTextureCompositorActiveJobs"},
        {"ViewportFrameMaxSize", "FIntViewportFrameMaxSize"},
        {"DebugGraphicsPreferD3D11", "FFlagDebugGraphicsPreferD3D11"},
        {"DebugGraphicsPreferOpenGL", "FFlagDebugGraphicsPreferOpenGL"},
        {"DisableDPIScale", "DFFlagDisableDPIScale"},
        {"CanHideGuiGroupId", "DFIntCanHideGuiGroupId"},
        {"FontSizePadding", "FIntFontSizePadding"},
        {"DebugForceMSAASamples", "FIntDebugForceMSAASamples"},
        {"RenderShadowmapBias", "FIntRenderShadowmapBias"},
        {"BandwidthManagerApplicationDefaultBps", "DFIntBandwidthManagerApplicationDefaultBps"},
        {"BandwidthManagerDataSenderMaxWorkCatchupMs", "DFIntBandwidthManagerDataSenderMaxWorkCatchupMs"},
        {"AssetPreloading", "DFIntAssetPreloading"},
        {"NumAssetsMaxToPreload", "DFIntNumAssetsMaxToPreload"},
        {"EnablePowerTraceModule", "FFlagEnablePowerTraceModule"},
        {"IncludePowerSaverMode", "FFlagIncludePowerSaverMode"},
        {"CoreGuiTypeSelfViewPresent", "FFlagCoreGuiTypeSelfViewPresent"},
        {"ReconnectDisabled", "FFlagReconnectDisabled"},
        {"HighlightOutlinesOnMobile", "FFlagHighlightOutlinesOnMobile"},
        {"TaskSchedulerAutoThreadLimit", "FIntTaskSchedulerAutoThreadLimit"},
        {"TaskSchedulerThreadMin", "FIntTaskSchedulerThreadMin"},
        {"GraphicsGLEnableSuperHQShadersExclusion", "FFlagGraphicsGLEnableSuperHQShadersExclusion"},
        {"GraphicsGLEnableHQShadersExclusion", "FFlagGraphicsGLEnableHQShadersExclusion"},
        {"RenderGpuTextureCompressor", "FFlagRenderGpuTextureCompressor"},
        {"DebugCheckRenderThreading", "FFlagDebugCheckRenderThreading"},
        {"GraphicsFixMsaaInGuiScene", "FFlagGraphicsFixMsaaInGuiScene"},
        {"DebugRenderingSetDeterministic", "FFlagDebugRenderingSetDeterministic"},
        {"FastGPULightCulling3", "FFlagFastGPULightCulling3"},
        {"DebugForceFSMCPULightCulling", "FFlagDebugForceFSMCPULightCulling"},
        {"DebugForceGenerateHSR", "FFlagDebugForceGenerateHSR"},
        {"DebugFRMOptionalMSAALevelOverride", "FIntDebugFRMOptionalMSAALevelOverride"},
        {"RenderLocalLightUpdatesMin", "FIntRenderLocalLightUpdatesMin"},
        {"RenderLocalLightUpdatesMax", "FIntRenderLocalLightUpdatesMax"},
        {"RenderLocalLightFadeInMs", "FIntRenderLocalLightFadeInMs"},
        {"RenderMaxShadowAtlasUsageBeforeDownscale", "FIntRenderMaxShadowAtlasUsageBeforeDownscale"},
        {"UnifiedLightingBlendZone", "FIntUnifiedLightingBlendZone"},
        {"HSRClusterSymmetryDistancePercent", "FIntHSRClusterSymmetryDistancePercent"},
        {"CoordinatorPlannerStepsPerIteration", "FIntCoordinatorPlannerStepsPerIteration"},
        {"RenderGrassDensityMax", "FIntRenderGrassDensityMax"},
        {"RenderGrassDensityMin", "FIntRenderGrassDensityMin"},
        {"GrassMovementReducedMotionFactor", "FIntGrassMovementReducedMotionFactor"},
        {"CameraMaxZoomDistance", "FIntCameraMaxZoomDistance"},
        {"CameraFarZPlane", "FIntCameraFarZPlane"},
        {"UITextureMaxRenderTextureSize", "FIntUITextureMaxRenderTextureSize"},
        {"UITextureMaxUpdateDepth", "FIntUITextureMaxUpdateDepth"},
        {"TerrainOTAMaxTextureSize", "FIntTerrainOTAMaxTextureSize"},
        {"DirectionalAttenuationMaxPoints", "FIntDirectionalAttenuationMaxPoints"},
        {"RenderClampRoughnessMax", "DFIntRenderClampRoughnessMax"},
        {"HACDPointSampleDistApartTenths", "DFIntHACDPointSampleDistApartTenths"},
        {"RenderingThrottleDelayInMS", "DFIntRenderingThrottleDelayInMS"},
        {"MaxFrameBufferSize", "DFIntMaxFrameBufferSize"},
        {"PerformanceControlTextureQualityBestUtility", "DFIntPerformanceControlTextureQualityBestUtility"},
        {"VisibilityCheckRayCastLimitPerFrame", "DFIntVisibilityCheckRayCastLimitPerFrame"},
        {"GraphicsOptimizationModeMaxFrameTimeTargetMs", "DFIntGraphicsOptimizationModeMaxFrameTimeTargetMs"},
        {"InterpolationDtLimitForLod", "DFIntInterpolationDtLimitForLod"},
        {"DebugPerfMode", "DFFlagDebugPerfMode"},
        {"DebugSkipMeshVoxelizer", "DFFlagDebugSkipMeshVoxelizer"},
        {"DebugTextureManagerSkipMips", "DFIntDebugTextureManagerSkipMips"},
        {"MeshContentProviderForceCacheSize", "FIntMeshContentProviderForceCacheSize"},
        {"DefaultMeshCacheSizeMB", "FIntDefaultMeshCacheSizeMB"},
        {"EmotesAnimationsPerPlayerCacheSize", "FIntEmotesAnimationsPerPlayerCacheSize"},
        {"HttpCurlConnectionCacheSize", "DFIntHttpCurlConnectionCacheSize"},
        {"MemoryUtilityCurveBaseHundrethsPercent", "DFIntMemoryUtilityCurveBaseHundrethsPercent"},
        {"MemoryUtilityCurveTotalMemoryReserve", "DFIntMemoryUtilityCurveTotalMemoryReserve"},
        {"MemoryUtilityCurveNumSegments", "DFIntMemoryUtilityCurveNumSegments"},
        {"UserIdPlayerNameCacheSize", "DFIntUserIdPlayerNameCacheSize"},
        {"NextGenReplicatorEnabledWrite", "FFlagNextGenReplicatorEnabledWrite"},
        {"NextGenReplicatorEnabledRead", "FFlagNextGenReplicatorEnabledRead"},
        {"UserUpdateInputConnections", "FFlagUserUpdateInputConnections"},
        {"UserCameraInputDt", "FFlagUserCameraInputDt"},
        {"UserCameraInputRefactor3", "FFlagUserCameraInputRefactor3"},
        {"UserCameraControlLastInputTypeUpdate", "FFlagUserCameraControlLastInputTypeUpdate"},
        {"BatchAssetApi", "FFlagBatchAssetApi"},
        {"LatencyLogging1", "FFlagLatencyLogging1"},
        {"ConnectionMTUSize", "DFIntConnectionMTUSize"},
        {"RakNetMtuValue1InBytes", "DFIntRakNetMtuValue1InBytes"},
        {"RakNetMtuValue2InBytes", "DFIntRakNetMtuValue2InBytes"},
        {"RakNetMtuValue3InBytes", "DFIntRakNetMtuValue3InBytes"},
        {"RakNetResendBufferArrayLength", "FIntRakNetResendBufferArrayLength"},
        {"RakNetDatagramMessageIdArrayLength", "FIntRakNetDatagramMessageIdArrayLength"},
        {"RakNetLoopMs", "DFIntRakNetLoopMs"},
        {"RakNetResendRttMultiple", "DFIntRakNetResendRttMultiple"},
        {"RaknetBandwidthPingSendEveryXSeconds", "DFIntRaknetBandwidthPingSendEveryXSeconds"},
        {"RakNetNakResendDelayMs", "DFIntRakNetNakResendDelayMs"},
        {"RakNetNakResendDelayMsMax", "DFIntRakNetNakResendDelayMsMax"},
        {"RakNetNakResendDelayRttPercent", "DFIntRakNetNakResendDelayRttPercent"},
        {"RakNetPingFrequencyMillisecond", "DFIntRakNetPingFrequencyMillisecond"},
        {"RakNetClockDriftAdjustmentPerPingMillisecond", "DFIntRakNetClockDriftAdjustmentPerPingMillisecond"},
        {"RakNetUseSlidingWindow2_minSpeed", "DFIntRakNetUseSlidingWindow2_minSpeed"},
        {"RakNetUseSlidingWindow2_maxSpeed", "DFIntRakNetUseSlidingWindow2_maxSpeed"},
        {"RakNetUseSlidingWindow2_startInitSpeed", "DFIntRakNetUseSlidingWindow2_startInitSpeed"},
        {"RakNetUseSlidingWindow2_startFactor", "DFIntRakNetUseSlidingWindow2_startFactor"},
        {"RakNetUseSlidingWindow2_trackLengthMs", "DFIntRakNetUseSlidingWindow2_trackLengthMs"},
        {"RakNetUseSlidingWindow2_startUpdateMs", "DFIntRakNetUseSlidingWindow2_startUpdateMs"},
        {"RakNetUseSlidingWindow2_rangeCount", "DFIntRakNetUseSlidingWindow2_rangeCount"},
        {"RakNetUseSlidingWindow2_minRtt", "DFIntRakNetUseSlidingWindow2_minRtt"},
        {"RaknetBandwidthInfluxHundredthsPercentageV2", "DFIntRaknetBandwidthInfluxHundredthsPercentageV2"},
        {"RakNetEnablePoll", "DFFlagRakNetEnablePoll"},
        {"RakNetDetectRecvThreadOverload", "DFFlagRakNetDetectRecvThreadOverload"},
        {"RakNetCalculateApplicationFeedback2", "DFFlagRakNetCalculateApplicationFeedback2"},
        {"RakNetUseSlidingWindow4", "DFFlagRakNetUseSlidingWindow4"},
        {"NetworkSchemaCompressionRatio", "DFIntNetworkSchemaCompressionRatio"},
        {"NetworkClusterPacketCacheNumParallelTasks", "DFIntNetworkClusterPacketCacheNumParallelTasks"},
        {"NetworkQualityResponderUnit", "DFIntNetworkQualityResponderUnit"},
        {"NetworkQualityResponderMaxWaitTime", "DFIntNetworkQualityResponderMaxWaitTime"},
        {"NetworkStopProducingPacketsToProcessThresholdMs", "DFIntNetworkStopProducingPacketsToProcessThresholdMs"},
        {"NetworkInProcessLimitGameplayMsClient", "DFIntNetworkInProcessLimitGameplayMsClient"},
        {"MaxProcessPacketsJobScaling", "DFIntMaxProcessPacketsJobScaling"},
        {"MaxProcessPacketsStepsAccumulated", "DFIntMaxProcessPacketsStepsAccumulated"},
        {"MaxProcessPacketsStepsPerCyclic", "DFIntMaxProcessPacketsStepsPerCyclic"},
        {"MaxDataPacketPerSend", "DFIntMaxDataPacketPerSend"},
        {"LargePacketQueueSizeCutoffMB", "DFIntLargePacketQueueSizeCutoffMB"},
        {"TimeBetweenSendConnectionAttemptsMS", "DFIntTimeBetweenSendConnectionAttemptsMS"},
        {"CodecMaxIncomingPackets", "DFIntCodecMaxIncomingPackets"},
        {"CodecMaxOutgoingFrames", "DFIntCodecMaxOutgoingFrames"},
        {"BufferCompressionThreshold", "DFIntBufferCompressionThreshold"},
        {"BufferCompressionLevel", "DFIntBufferCompressionLevel"},
        {"ClusterCompressionLevel", "DFIntClusterCompressionLevel"},
        {"JoinDataCompressionLevel", "DFIntJoinDataCompressionLevel"},
        {"JoinDataItemEstimatedCompressionRatioHundreths", "DFIntJoinDataItemEstimatedCompressionRatioHundreths"},
        {"ClusterEstimatedCompressionRatioHundredths", "DFIntClusterEstimatedCompressionRatioHundredths"},
        {"MegaReplicatorNumParallelTasks", "DFIntMegaReplicatorNumParallelTasks"},
        {"MegaReplicatorNetworkQualityProcessorUnit", "DFIntMegaReplicatorNetworkQualityProcessorUnit"},
        {"ReplicationDataCacheNumParallelTasks", "DFIntReplicationDataCacheNumParallelTasks"},
        {"DataSenderMaxBandwidthBps", "DFIntDataSenderMaxBandwidthBps"},
        {"DataSenderMaxJoinBandwidthBps", "DFIntDataSenderMaxJoinBandwidthBps"},
        {"DataSenderRate", "DFIntDataSenderRate"},
        {"S2PhysicsSenderRate", "DFIntS2PhysicsSenderRate"},
        {"SendGameServerDataMaxLen", "DFIntSendGameServerDataMaxLen"},
        {"WaitOnRecvFromLoopEndedMS", "DFIntWaitOnRecvFromLoopEndedMS"},
        {"WaitOnUpdateNetworkLoopEndedMS", "DFIntWaitOnUpdateNetworkLoopEndedMS"},
        {"ClientPacketMaxDelayMs", "DFIntClientPacketMaxDelayMs"},
        {"ClientPacketMaxFrameMicroseconds", "DFIntClientPacketMaxFrameMicroseconds"},
        {"ClientPacketHealthyAllocationPercent", "DFIntClientPacketHealthyAllocationPercent"},
        {"ClientPacketExcessMicroseconds", "DFIntClientPacketExcessMicroseconds"},
        {"InterpolationFrameVelocityThresholdMillionth", "DFIntInterpolationFrameVelocityThresholdMillionth"},
        {"InterpolationNumMechanismsPerTask", "DFIntInterpolationNumMechanismsPerTask"},
        {"InterpolationNumMechanismsBatchSize", "DFIntInterpolationNumMechanismsBatchSize"},
        {"InterpolationNumParallelTasks", "DFIntInterpolationNumParallelTasks"},
        {"InterpolationMinAssemblyCount", "DFIntInterpolationMinAssemblyCount"},
        {"InterpolationFrameRotVelocityThresholdMillionth", "DFIntInterpolationFrameRotVelocityThresholdMillionth"},
        {"InterpolationMaxDelayMSec", "FIntInterpolationMaxDelayMSec"},
        {"InterpolationAwareTargetTimeLerpHundredth", "FIntInterpolationAwareTargetTimeLerpHundredth"},
        {"NumFramesAllowedToBeAboveError", "DFIntNumFramesAllowedToBeAboveError"},
        {"NumFramesToKeepAfterInterpolation", "DFIntNumFramesToKeepAfterInterpolation"},
        {"SkipSomePropertiesPermyriad", "DFIntSkipSomePropertiesPermyriad"},
        {"ReplicatorCheckReadTableCollisions", "DFFlagReplicatorCheckReadTableCollisions"},
        {"ReplicatorDataPingReportHundredthPercentage", "DFIntReplicatorDataPingReportHundredthPercentage"},
        {"ReplicatorJdiReportThrottlePercent", "DFIntReplicatorJdiReportThrottlePercent"},
        {"ReplicatorDataPingReportThrottleSeconds", "DFIntReplicatorDataPingReportThrottleSeconds"},
        {"ReplicatorDataPingReportThresholdMs", "DFIntReplicatorDataPingReportThresholdMs"},
        {"ClientReplicatorStatsEventsThrottleHP", "DFIntClientReplicatorStatsEventsThrottleHP"},
        {"ReplicatorSeparateVarThresholds", "DFFlagReplicatorSeparateVarThresholds"},
        {"DebugUpdateClientChannelB", "DFFlagDebugUpdateClientChannelB"},
        {"PhysicsReceiveNumParallelTasks", "DFIntPhysicsReceiveNumParallelTasks"},
        {"PhysicsSenderMaxBandwidthBps", "DFIntPhysicsSenderMaxBandwidthBps"},
        {"SimWorldTaskQueueParallelTasks", "FIntSimWorldTaskQueueParallelTasks"},
        {"SmoothClusterTaskQueueMaxParallelTasks", "FIntSmoothClusterTaskQueueMaxParallelTasks"},
        {"TimestepArbiterThresholdCFLThou", "DFIntTimestepArbiterThresholdCFLThou"},
        {"TimestepArbiterAngAccelerationThresholdThou", "DFIntTimestepArbiterAngAccelerationThresholdThou"},
        {"TimestepArbiterAccelerationModelFactorThou", "DFIntTimestepArbiterAccelerationModelFactorThou"},
        {"SolidFloorPercentForceApplication", "DFIntSolidFloorPercentForceApplication"},
        {"AnimatorThrottleMaxFramesToSkip", "DFIntAnimatorThrottleMaxFramesToSkip"},
        {"AirControllerTurningResponsiveness", "DFIntAirControllerTurningResponsiveness"},
        {"MaximumFreefallMoveTimeInTenths", "DFIntMaximumFreefallMoveTimeInTenths"},
        {"MaxMissedWorldStepsRemembered", "DFIntMaxMissedWorldStepsRemembered"},
        {"MaxAcceptableUpdateDelay", "DFIntMaxAcceptableUpdateDelay"},
        {"OptimizedContactPipelineSpatialHashOverlapToleranceMicro", "FIntOptimizedContactPipelineSpatialHashOverlapToleranceMicro"},
        {"ThrottlingPredictionAccelerationHoldThousandth", "DFIntThrottlingPredictionAccelerationHoldThousandth"},
        {"InitialAccelerationLatencyMultTenths", "DFIntInitialAccelerationLatencyMultTenths"},
        {"OptimizePartsInPart", "DFFlagOptimizePartsInPart"},
        {"JointIrregularityOptimization", "DFFlagJointIrregularityOptimization"},
        {"DebugStreamingFastRecovery", "DFFlagDebugStreamingFastRecovery"},
        {"SimCSG3DcdMaxContacts", "DFIntSimCSG3DcdMaxContacts"},
        {"SimCSG3DCDStats", "DFFlagSimCSG3DCDStats"},
        {"PhysicsAnalyticsHighFrequencyIntervalSec", "DFIntPhysicsAnalyticsHighFrequencyIntervalSec"},
        {"SimConstraintDataCollectionRate3", "DFIntSimConstraintDataCollectionRate3"},
        {"RagdollDefaultTimerTenthSecond", "DFIntRagdollDefaultTimerTenthSecond"},
        {"RagdollSlowEnoughToNotTenth", "DFIntRagdollSlowEnoughToNotTenth"},
        {"RagdollEarlyExitTimeTenthSecond", "DFIntRagdollEarlyExitTimeTenthSecond"},
        {"SmoothMouseSpringFrequencyTenths", "FIntSmoothMouseSpringFrequencyTenths"},
        {"LuauCodegen", "FFlagLuauCodegen"},
        {"LuauSolverV2", "FFlagLuauSolverV2"},
        {"LuaGcParallelMinMultiTasks", "FIntLuaGcParallelMinMultiTasks"},
        {"AnimationClipMemCacheEnabled", "FFlagAnimationClipMemCacheEnabled"},
        {"AssetPreloadingIXP", "FFlagAssetPreloadingIXP"},
        {"PreloadTextureItemsOption4", "FFlagPreloadTextureItemsOption4"},
        {"EnableTexturePreloading", "DFFlagEnableTexturePreloading"},
        {"EnableMeshPreloading2", "DFFlagEnableMeshPreloading2"},
        {"EnableSoundPreloading", "DFFlagEnableSoundPreloading"},
        {"TeleportClientAssetPreloadingDoingExperiment", "DFFlagTeleportClientAssetPreloadingDoingExperiment"},
        {"TeleportClientAssetPreloadingDoingExperiment2", "DFFlagTeleportClientAssetPreloadingDoingExperiment2"},
        {"TeleportClientAssetPreloadingEnabledIXP", "DFFlagTeleportClientAssetPreloadingEnabledIXP"},
        {"TeleportClientAssetPreloadingEnabledIXP2", "DFFlagTeleportClientAssetPreloadingEnabledIXP2"},
        {"TeleportClientAssetPreloadingHundredthsPercentage", "DFIntTeleportClientAssetPreloadingHundredthsPercentage"},
        {"TeleportClientAssetPreloadingHundredthsPercentage2", "DFIntTeleportClientAssetPreloadingHundredthsPercentage2"},
        {"CharacterLoadTime", "DFIntCharacterLoadTime"},
        {"FastClusterHumanoidWorstCaseGeometryDataBytes", "FIntFastClusterHumanoidWorstCaseGeometryDataBytes"},
        {"FastClusterHumanoidWorstCaseBytes", "FIntFastClusterHumanoidWorstCaseBytes"},
        {"FastClusterUpdateWaitingBudgetMs", "FIntFastClusterUpdateWaitingBudgetMs"},
        {"CachedPatchLoadDelayMilliseconds", "DFIntCachedPatchLoadDelayMilliseconds"},
        {"HttpBatchApi_bgDelayMs", "DFIntHttpBatchApi_bgDelayMs"},
        {"HttpBatchApi_bgRefreshMaxDelayMs", "DFIntHttpBatchApi_bgRefreshMaxDelayMs"},
        {"HttpBatchApi_maxWaitMs", "DFIntHttpBatchApi_maxWaitMs"},
        {"HttpBatchApi_maxReqs", "DFIntHttpBatchApi_maxReqs"},
        {"HttpBatchApi_MaxBatchesSentPerCyle", "DFIntHttpBatchApi_MaxBatchesSentPerCyle"},
        {"HttpBatchApi_minWaitMs", "DFIntHttpBatchApi_minWaitMs"},
        {"HttpBatchApi_cacheDelayMs", "DFIntHttpBatchApi_cacheDelayMs"},
        {"BatchThumbnailResultsSizeCap", "DFIntBatchThumbnailResultsSizeCap"},
        {"BatchThumbnailMinWaitMs", "DFIntBatchThumbnailMinWaitMs"},
        {"BatchThumbnailMaxWaitMs", "DFIntBatchThumbnailMaxWaitMs"},
        {"BatchThumbnailJobRunsPerSecond", "DFIntBatchThumbnailJobRunsPerSecond"},
        {"BatchThumbnailExponentialInitialWaitMs", "DFIntBatchThumbnailExponentialInitialWaitMs"},
        {"BatchThumbnailLimit", "DFIntBatchThumbnailLimit"},
        {"BatchThumbnailMaxExponentialRetries", "DFIntBatchThumbnailMaxExponentialRetries"},
        {"BatchThumbnailMaxReqests", "DFIntBatchThumbnailMaxReqests"},
        {"BatchThumbnailExperiationTimeSeconds", "DFIntBatchThumbnailExperiationTimeSeconds"},
        {"BatchThumbnailAllowedExternalTimedOutRetries", "DFIntBatchThumbnailAllowedExternalTimedOutRetries"},
        {"HttpRbxApiRequestsPerMinuteServerLimit", "DFIntHttpRbxApiRequestsPerMinuteServerLimit"},
        {"HttpRbxApiRequestsPerMinutePerPlayerInServerLimit", "DFIntHttpRbxApiRequestsPerMinutePerPlayerInServerLimit"},
        {"HttpRbxApiMaxThrottledQueueSize", "DFIntHttpRbxApiMaxThrottledQueueSize"},
        {"HttpRbxApiJobFrequencyInSeconds", "DFIntHttpRbxApiJobFrequencyInSeconds"},
        {"HttpRbxApiMaxBudgetMultiplier", "DFIntHttpRbxApiMaxBudgetMultiplier"},
        {"HttpRbxApiSyncRetryWaitTimeMSec", "DFIntHttpRbxApiSyncRetryWaitTimeMSec"},
        {"HttpRbxApiMaxSyncRetries", "DFIntHttpRbxApiMaxSyncRetries"},
        {"HttpRbxApiMaxRetryQueueSize", "DFIntHttpRbxApiMaxRetryQueueSize"},
        {"HttpRbxApiMaxRetryCount", "DFIntHttpRbxApiMaxRetryCount"},
        {"HttpRbxApiMaxRetryBudgetPerMinute", "DFIntHttpRbxApiMaxRetryBudgetPerMinute"},
        {"HttpRbxApiClientPerMinuteRequestLimit", "DFIntHttpRbxApiClientPerMinuteRequestLimit"},
        {"DownloadEpisodeTimerFreqeuncyInMs", "DFIntDownloadEpisodeTimerFreqeuncyInMs"},
        {"RaknetDownloadEpisodeInMs", "DFIntRaknetDownloadEpisodeInMs"},
        {"ConnectingTimerInterval", "DFIntConnectingTimerInterval"},
        {"RingingTimerInterval", "DFIntRingingTimerInterval"},
        {"SignalRCoreTimerMs", "DFIntSignalRCoreTimerMs"},
        {"SignalRCoreServerTimeoutMs", "DFIntSignalRCoreServerTimeoutMs"},
        {"SignalRCoreRpcQueueSize", "DFIntSignalRCoreRpcQueueSize"},
        {"SignalRCoreKeepAlivePingPeriodMs", "DFIntSignalRCoreKeepAlivePingPeriodMs"},
        {"SignalRCoreHubMaxElapsedMs", "DFIntSignalRCoreHubMaxElapsedMs"},
        {"SignalRCoreHubMaxBackoffMs", "DFIntSignalRCoreHubMaxBackoffMs"},
        {"SignalRCoreHubBaseRetryMs", "DFIntSignalRCoreHubBaseRetryMs"},
        {"SignalRCoreHandshakeTimeoutMs", "DFIntSignalRCoreHandshakeTimeoutMs"},
        {"SignalRCoreHubConnectionDisconnectInfoHundredthsPercent", "DFIntSignalRCoreHubConnectionDisconnectInfoHundredthsPercent"},
        {"ServerBandwidthPlayerSampleRate", "DFIntServerBandwidthPlayerSampleRate"},
        {"DisableFastLogTelemetry", "DFFlagDisableFastLogTelemetry"},
        {"EnablePerfAudioCollection", "DFFlagEnablePerfAudioCollection"},
        {"EnablePerfDataCoreCategoryTimersCollection2", "DFFlagEnablePerfDataCoreCategoryTimersCollection2"},
        {"EnablePerfDataCountersCollection", "DFFlagEnablePerfDataCountersCollection"},
        {"EnablePerfDataGatherTelemetry2", "DFFlagEnablePerfDataGatherTelemetry2"},
        {"EnablePercentileTelemetry", "DFFlagEnablePercentileTelemetry"},
        {"ClientRolloutPhaseTelemetry", "DFFlagClientRolloutPhaseTelemetry"},
        {"ContentProviderPreloadHangTelemetry", "FFlagContentProviderPreloadHangTelemetry"},
        {"LocServicePerformanceAnalyticsEnabled", "FFlagLocServicePerformanceAnalyticsEnabled"},
        {"StartupTracingInfluxRollout", "DFIntStartupTracingInfluxRollout"},
        {"ClientLightingTechnologyChangedTelemetryHundredthsPercent", "DFIntClientLightingTechnologyChangedTelemetryHundredthsPercent"},
        {"CrashUploadToBacktracePercentage", "DFIntCrashUploadToBacktracePercentage"},
        {"ReportDeviceInfoRollout", "FIntReportDeviceInfoRollout"},
        {"CoreScriptsAnalyticsHundredthsPercentage", "DFIntCoreScriptsAnalyticsHundredthsPercentage"},
        {"ConnectionLostDisconnectReasonInfluxHundredthsPercentage", "DFIntConnectionLostDisconnectReasonInfluxHundredthsPercentage"},
        {"BandwidthManagerFeedbackInfluxHundredthsPercentage", "DFIntBandwidthManagerFeedbackInfluxHundredthsPercentage"},
        {"BgUpdateRedirectsRejectInfluxHundredthsPercentage", "DFIntBgUpdateRedirectsRejectInfluxHundredthsPercentage"},
        {"BgUpdateRedirectsHttpErrInfluxHundredthsPercentage", "DFIntBgUpdateRedirectsHttpErrInfluxHundredthsPercentage"},
        {"DebugPrintDataPingBreakDown", "DFFlagDebugPrintDataPingBreakDown"},
        {"FacialAnimationStreamingServiceUserSettingsOptInVideo", "FFlagFacialAnimationStreamingServiceUserSettingsOptInVideo"},
        {"FacialAnimationStreamingServiceUserSettingsOptInAudio", "FFlagFacialAnimationStreamingServiceUserSettingsOptInAudio"},
        {"AnimatorRetargetSkipLowerBodyIKDistance10x", "DFIntAnimatorRetargetSkipLowerBodyIKDistance10x"},
        {"AnimatorRetargetInterpolateFKCorrectionMinAngleDeg", "DFIntAnimatorRetargetInterpolateFKCorrectionMinAngleDeg"},
        {"AnimatorRetargetInterpolateFKCorrectionMaxAngleDeg", "DFIntAnimatorRetargetInterpolateFKCorrectionMaxAngleDeg"},
        {"EnableAnimatorRetargetingMode", "DFFlagEnableAnimatorRetargetingMode"},
        {"AllowRegistrationOfAnimationClipInCoreScripts", "DFFlagAllowRegistrationOfAnimationClipInCoreScripts"},
        {"ActivatedCountTimerMSMouse", "FIntActivatedCountTimerMSMouse"},
        {"ActivatedCountTimerMSTouch", "FIntActivatedCountTimerMSTouch"},
        {"ActivatedCountTimerMSKeyboard", "FIntActivatedCountTimerMSKeyboard"},
        {"NumTrackerDataToReplicatePerFrame", "DFIntNumTrackerDataToReplicatePerFrame"},
        {"GuiHidingApiSupport2", "FFlagGuiHidingApiSupport2"},
        {"UserShowGuiHideToggles", "FFlagUserShowGuiHideToggles"},
        {"RemoveLegacyPackageAnalytics", "DFFlagRemoveLegacyPackageAnalytics"},
        {"ExperienceSignalsIngestMaxEventsPerBatch", "DFIntExperienceSignalsIngestMaxEventsPerBatch"},
        {"RemoteEventAbuseReportRateLimitMax", "DFIntRemoteEventAbuseReportRateLimitMax"},
        {"CloseReplicationDataAliveForAllHundredthsPercentage", "DFIntCloseReplicationDataAliveForAllHundredthsPercentage"},
        {"ClientNetworkInfluxHundredthsPercentage", "DFIntClientNetworkInfluxHundredthsPercentage"},
        {"RenderInitShadowmaps", "FFlagRenderInitShadowmaps"},
        {"RenderEnableGlobalInstancingD3D11", "FFlagRenderEnableGlobalInstancingD3D11"},
        {"DebugDisplayFPS", "FFlagDebugDisplayFPS"},
        
        
        {"GlobalWind", "FFlagGlobalWind"},
        {"HumanoidParallelMinParallelSize", "DFIntHumanoidParallelMinParallelSize"},
        {"HumanoidParallelThreadMax", "DFIntHumanoidParallelThreadMax"},
        {"HumanoidParallelMultiSubmitBatchSize", "DFIntHumanoidParallelMultiSubmitBatchSize"},
        {"DebugLuauCodegenAll", "DebugLuauCodegenAll"},
        {"FacialAnimationStreamingUseEnableFlags2", "FFlagFacialAnimationStreamingUseEnableFlags2"},
    };
    int ok = 0;
    int total = (int)(sizeof(wants) / sizeof(wants[0]));
    for (auto& w : wants)
        if (DumpOne(w)) ++ok;
    WriteFFlagsHpp();
    
    
    if (ok == total)
        LogOk("FFLAGS FLOW OK");
    else
        LogBad("FFLAGS FLOW PARTIAL (%d/%d) — see MISS lines", ok, total);
    return ok > 0;
}

}  
