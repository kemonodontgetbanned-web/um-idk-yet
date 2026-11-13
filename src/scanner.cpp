#include "scanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <unistd.h>
#include <vector>

#include "globals.h"
#include "mem.h"
#include "log.h"

void Emit(const char* name, const char* cat, uintptr_t off, const char* ty) {
    LogOk("OFFSET %-28s %-14s 0x%lx (%lu)  [%s]", name, cat, (unsigned long)off, (unsigned long)off, ty);
    for (auto& e : g_emitted)
        if (e.cat == cat && e.name == name) return;  
    g_emitted.push_back({cat, name, ty, off});
}

bool Correct(const char* name, const char* cat, uintptr_t off) {
    for (auto& e : g_emitted)
        if (e.cat == cat && e.name == name) {
            if (e.val == off) return true;
            LogOk("CORRECTED %-25s %-14s 0x%lx -> 0x%lx  [%s]", name, cat,
                  (unsigned long)e.val, (unsigned long)off, e.ty.c_str());
            e.val = off;
            return true;
        }
    return false;
}

void WriteHpp() {
    auto ExecCommand = [](const std::string& cmd) -> std::string {
        std::string out;
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return out;
        char b[512];
        while (fgets(b, sizeof(b), p)) out += b;
        pclose(p);
        return out;
    };
    
    std::string md5 = "unknown";
    {
        std::string cmd = "md5sum '" + g_dumpedPath + "' 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r");
        if (p) {
            char b[64] = {};
            size_t n = fread(b, 1, sizeof(b) - 1, p);
            pclose(p);
            std::string s(b, n);
            if (s.size() >= 32 &&
                s.substr(0, 32).find_first_not_of("0123456789abcdef") ==
                    std::string::npos)
                md5 = s.substr(0, 32);
        }
    }
    
    std::string apkVer = "unknown";
    {
        const char* home = getenv("HOME");
        std::string apkBase;
        if (home)
            apkBase = std::string(home) +
                      "/.var/app/org.vinegarhq.Sober/data/sober/packages/"
                      "x86_64/com.roblox.client";
        std::string apk = apkBase + "/base.apk";
        FILE* t = fopen(apk.c_str(), "r");
        if (t) {
            fclose(t);
            std::string RawVersion = ExecCommand(
                "aapt dump badging \"" + apk + "\" 2>/dev/null | grep versionName");
            size_t p = RawVersion.find("versionName='");
            if (p != std::string::npos) {
                p += 13;
                size_t e = RawVersion.find('\'', p);
                if (e != std::string::npos && e > p)
                    apkVer = RawVersion.substr(p, e - p);
            }
        }
    }
    std::string base = g_dumpedPath;
    size_t sl = base.find_last_of('/');
    if (sl != std::string::npos) base = base.substr(sl + 1);
    
    
    
    
    
    
    
    
    
    std::string prevMd5;
    {
        FILE* o = fopen("Offsets.hpp", "r");
        if (o) {
            char ln[512];
            while (fgets(ln, sizeof(ln), o)) {
                std::string s = ln;
                size_t p = s.find("(md5 ");
                if (p != std::string::npos && s.size() >= p + 5 + 32) {
                    std::string m = s.substr(p + 5, 32);
                    if (m.find_first_not_of("0123456789abcdef") ==
                        std::string::npos)
                        prevMd5 = m;
                    break;
                }
            }
            fclose(o);
        }
    }
    if (!prevMd5.empty() && prevMd5 != md5) {
        LogBad("version changed (md5 %s -> %s): NOT carrying old offsets",
               prevMd5.c_str(), md5.c_str());
    } else {
        FILE* o = fopen("Offsets.hpp", "r");
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
                if (sscanf(s.c_str(),
                           " inline constexpr uintptr_t %127s = 0x%lx", nm,
                           &v) == 2) {
                    std::string n = nm;
                    old.push_back({cat, n, (uintptr_t)v});
                }
            }
            fclose(o);
            for (auto& e : old) {
                if ((e.cat == "DataModel" && e.name == "ServerIP") ||
                    (e.cat == "DataModel" && e.name == "JobId"))
                    continue;
                if (e.cat == "FakeDataModel" && e.name == "Pointer") {
                    LogBad("DROPPED %-28s %-14s 0x%lx (no image static since 2.737)",
                           e.name.c_str(), e.cat.c_str(),
                           (unsigned long)e.val);
                    continue;
                }
                bool have = false;
                for (auto& g : g_emitted)
                    if (g.cat == e.cat && g.name == e.name) {
                        have = true;
                        break;
                    }
                if (!have) {
                    g_emitted.push_back({e.cat, e.name, "kept-old", e.val});
                    LogInfo("KEPT-OLD %-28s %-14s 0x%lx", e.name.c_str(),
                            e.cat.c_str(), (unsigned long)e.val);
                    ++kept;
                }
            }
            if (kept)
                LogInfo("KEPT-OLD total %d offsets from previous header",
                        kept);
        }
    }
    FILE* f = fopen("Offsets.hpp", "w");
    if (!f) {
        LogBad("could not write Offsets.hpp");
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
        if (std::find(order.begin(), order.end(), e.cat) == order.end())
            order.push_back(e.cat);
    for (size_t ci = 0; ci < order.size(); ++ci) {
        fprintf(f, "    namespace %s {\n", order[ci].c_str());
        for (auto& e : g_emitted) {
            if (e.cat != order[ci]) continue;
            fprintf(f, "        inline constexpr uintptr_t %s = 0x%lx;\n",
                    e.name.c_str(), (unsigned long)e.val);
        }
        fprintf(f, "    }%s", ci + 1 < order.size() ? "\n\n" : "\n");
    }
    fprintf(f, "}\n");
    fclose(f);
    {
        char cwd[4096] = {};
        if (!getcwd(cwd, sizeof(cwd) - 1)) cwd[0] = 0;
        LogOk("wrote %s/Offsets.hpp (%zu offsets)", cwd, g_emitted.size());
    }
    
    
}

void WriteJson() {
    FILE* f = fopen("Offsets.json", "w");
    if (!f) {
        LogBad("could not write Offsets.json");
        return;
    }
    fprintf(f, "{\n");
    std::vector<std::string> order;
    for (auto& e : g_emitted)
        if (std::find(order.begin(), order.end(), e.cat) == order.end())
            order.push_back(e.cat);
    for (size_t ci = 0; ci < order.size(); ++ci) {
        fprintf(f, "  \"%s\": {\n", order[ci].c_str());
        bool first = true;
        for (auto& e : g_emitted) {
            if (e.cat != order[ci]) continue;
            fprintf(f, "%s    \"%s\": %lu", first ? "" : ",\n", e.name.c_str(),
                    (unsigned long)e.val);
            first = false;
        }
        fprintf(f, "\n  }%s\n", ci + 1 < order.size() ? "," : "");
    }
    fprintf(f, "}\n");
    fclose(f);
    LogOk("wrote Offsets.json (%zu offsets)", g_emitted.size());
}




static std::string FileMd5(const std::string& path) {
    std::string cmd = "md5sum '" + path + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "unknown";
    char b[64] = {};
    size_t n = fread(b, 1, sizeof(b) - 1, p);
    pclose(p);
    std::string s(b, n);
    if (s.size() >= 32 &&
        s.substr(0, 32).find_first_not_of("0123456789abcdef") ==
            std::string::npos)
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
    FILE* p = popen(
        ("aapt dump badging \"" + apk +
         "\" 2>/dev/null | grep versionName")
            .c_str(),
        "r");
    if (!p) return "unknown";
    std::string out;
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

static bool CopyFile(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    out << in.rdbuf();
    out.flush();
    return (bool)out;
}




static const char* StructTy(const std::string& ty, size_t& sz) {
    if (ty == "float") return (sz = 4, "float");
    if (ty == "double") return (sz = 8, "double");
    if (ty == "int") return (sz = 4, "int");
    if (ty == "short") return (sz = 2, "short");
    if (ty == "bool") return (sz = 1, "bool");
    if (ty == "BYTE") return (sz = 1, "BYTE");
    if (ty == "string") return (sz = 8, "std::string");
    if (ty == "unsigned int") return (sz = 4, "unsigned int");
    if (ty == "Vector3") return (sz = 12, "Vector3");
    if (ty == "Vector2") return (sz = 8, "Vector2");
    if (ty == "Vector4") return (sz = 16, "Vector4");
    if (ty == "Matrix3x3") return (sz = 36, "Matrix3x3");
    if (ty == "Matrix4x4") return (sz = 64, "Matrix4x4");
    if (ty == "CFrame") return (sz = 48, "CFrame");
    if (ty == "Color3") return (sz = 12, "Color3");
    return (sz = 8, "uintptr_t");  
}

static std::string SanitizeMember(const std::string& n) {
    std::string o;
    for (char c : n) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_')
            o += c;
        else
            o += '_';
    }
    if (o.empty() || (o[0] >= '0' && o[0] <= '9')) o = "_" + o;
    return o;
}

void WriteStruct() {
    std::string md5 = FileMd5(g_dumpedPath);
    std::string apkVer = ApkVersion();
    std::string base = g_dumpedPath;
    size_t sl = base.find_last_of('/');
    if (sl != std::string::npos) base = base.substr(sl + 1);
    char date[64] = {};
    {
        time_t now = time(nullptr);
        struct tm* g = gmtime(&now);
        if (g) strftime(date, sizeof(date), "%H:%M %d/%m/%Y (GMT)", g);
    }
    FILE* f = fopen("struct.hpp", "w");
    if (!f) {
        LogBad("could not write struct.hpp");
        return;
    }
    fprintf(f, "#pragma once\n");
    fprintf(f, "// dumped by idom - ix dumper v2.3 (struct layout)\n");
    fprintf(f, "// build: %s (md5 %s)\n", base.c_str(), md5.c_str());
    fprintf(f, "// apk version: %s\n", apkVer.c_str());
    fprintf(f, "// dumped at: %s\n", date);
    fprintf(f, "// total offsets: %zu\n", g_emitted.size());
    fprintf(f, "#include <cstdint>\n\n#include <string>\n\n");
    fprintf(f, "namespace Structs {\n");
    fprintf(f, "    inline std::string ClientVersion = \"%s\";\n\n",
            apkVer.c_str());
    fprintf(f, "    using BYTE = unsigned char;\n");
    fprintf(f, "    struct Vector2 { float x, y; };\n");
    fprintf(f, "    struct Vector3 { float x, y, z; };\n");
    fprintf(f, "    struct Vector4 { float x, y, z, w; };\n");
    fprintf(f, "    struct Matrix3x3 { float m[9]; };\n");
    fprintf(f, "    struct Matrix4x4 { float m[16]; };\n");
    fprintf(f, "    struct Color3 { float r, g, b; };\n");
    fprintf(f, "    struct CFrame { Matrix3x3 rot; Vector3 pos; };\n");
    std::vector<std::string> order;
    for (auto& e : g_emitted)
        if (std::find(order.begin(), order.end(), e.cat) == order.end())
            order.push_back(e.cat);
    int pc = 0;
    for (size_t ci = 0; ci < order.size(); ++ci) {
        std::vector<Emitted> es;
        for (auto& e : g_emitted)
            if (e.cat == order[ci]) es.push_back(e);
        std::sort(es.begin(), es.end(),
                  [](const Emitted& a, const Emitted& b) {
                      return a.val < b.val;
                  });
        fprintf(f, "\n    struct %s {\n", order[ci].c_str());
        uintptr_t pos = 0;
        for (auto& e : es) {
            size_t sz = 8;
            const char* mty = StructTy(e.ty, sz);
            if (e.val > pos) {
                fprintf(f, "        char pad_%d[0x%lx];\n", pc++,
                        (unsigned long)(e.val - pos));
                pos = e.val;
            }
            std::string mn = SanitizeMember(e.name);
            if (mn == mty) mn += "_";  
            if (e.ty == "kept-old")
                fprintf(f,
                        "        %s %s; // 0x%lx (kept-old: exact type "
                        "unknown, laid out pointer-sized)\n",
                        mty, mn.c_str(), (unsigned long)e.val);
            else
                fprintf(f, "        %s %s; // 0x%lx\n", mty, mn.c_str(),
                        (unsigned long)e.val);
            if (e.val + sz > pos) pos = e.val + sz;
        }
        fprintf(f, "    }; // sizeof = %lx\n", (unsigned long)pos);
    }
    fprintf(f, "}\n");
    fclose(f);
    char cwd[4096] = {};
    if (!getcwd(cwd, sizeof(cwd) - 1)) cwd[0] = 0;
    LogOk("wrote %s/struct.hpp (%zu offsets)", cwd, g_emitted.size());
}




void Publish() {
    const char* noPub = getenv("IX_NO_PUBLISH");
    const char* home = getenv("HOME");
    if (noPub && noPub[0]) {
        LogInfo("[publish] skipped (IX_NO_PUBLISH set)");
        return;
    }
    if (!home) {
        LogBad("[publish] skipped (no HOME)");
        return;
    }
    std::string md5 = FileMd5(g_dumpedPath);
    std::string apkVer = ApkVersion();
    std::string upDir = std::string(home) + "/offsets_upload";
    char cwd2[4096] = {};
    std::string cwd =
        getcwd(cwd2, sizeof(cwd2) - 1) ? std::string(cwd2) : ".";
    bool ok = true;
    if (!CopyFile(cwd + "/Offsets.hpp", upDir + "/Offsets.hpp")) {
        LogBad("[publish] copy failed (Offsets.hpp)");
        ok = false;
    }
    if (!CopyFile(cwd + "/struct.hpp", upDir + "/struct.hpp")) {
        LogBad("[publish] copy failed (struct.hpp)");
        ok = false;
    }
    
    
    std::string vdir = upDir + "/version/" + apkVer;
    if (ok && system(("mkdir -p \"" + vdir + "\"").c_str()) == 0) {
        if (!CopyFile(cwd + "/Offsets.hpp", vdir + "/Offsets.hpp") ||
            !CopyFile(cwd + "/struct.hpp", vdir + "/struct.hpp")) {
            LogBad("[publish] archive copy failed (%s)", vdir.c_str());
            ok = false;
        } else {
            LogOk("[publish] archived %s", vdir.c_str());
        }
    }
    if (ok) {
        
        
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
                    char b1[64] = {}, b2[64] = {}, b3[256] = {},
                         b4[256] = {};
                    const char* s = blk.c_str();
                    auto grab = [&](const char* key, char* dst, size_t n) {
                        std::string k = std::string("\"") + key + "\"";
                        size_t p = blk.find(k);
                        if (p == std::string::npos) return false;
                        p = blk.find('"', p + k.size());
                        if (p == std::string::npos) return false;
                        size_t q = blk.find('"', p + 1);
                        if (q == std::string::npos) return false;
                        snprintf(dst, n, "%s",
                                 blk.substr(p + 1, q - p - 1).c_str());
                        (void)s;
                        return true;
                    };
                    bool hasApk = grab("apk", b1, sizeof(b1));
                    grab("md5", b2, sizeof(b2));
                    grab("path", b3, sizeof(b3));
                    grab("struct", b4, sizeof(b4));
                    char b5[256] = {};
                    grab("luau", b5, sizeof(b5));
                    char b6[256] = {};
                    grab("fflags", b6, sizeof(b6));
                    size_t cnt = 0;
                    size_t pc2 = blk.find("\"offsets\"");
                    if (pc2 != std::string::npos) {
                        size_t cc = blk.find(':', pc2);
                        if (cc != std::string::npos)
                            cnt = strtoul(blk.c_str() + cc + 1, nullptr,
                                          10);
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
                    if (s.find('{') != std::string::npos && !blk.empty())
                        flush();
                    if (s.find('{') != std::string::npos ||
                        !blk.empty() || s.find('}') != std::string::npos)
                        blk += s;
                    if (s.find('}') != std::string::npos) flush();
                }
                flush();
                fclose(vf);
            }
        }
        VEnt cur{apkVer,
                  md5,
                  "version/" + apkVer + "/Offsets.hpp",
                  "version/" + apkVer + "/struct.hpp",
                  "",
                  "",
                  g_emitted.size()};
        bool replaced = false;
        for (auto& e : ents)
            if (e.apk == cur.apk || (!cur.md5.empty() && e.md5 == cur.md5 &&
                                     cur.md5 != "unknown")) {
                cur.luau = e.luau;  
                cur.ff = e.ff;      
                e = cur;
                replaced = true;
            }
        if (!replaced) ents.insert(ents.begin(), cur);
        {
            FILE* vf = fopen(vpath.c_str(), "w");
            if (!vf) {
                LogBad("[publish] cannot write versions index");
                ok = false;
            } else {
                fprintf(vf, "[\n");
                for (size_t i = 0; i < ents.size(); ++i) {
                    auto& e = ents[i];
                    fprintf(vf, "  {\n");
                    fprintf(vf, "    \"apk\": \"%s\",\n", e.apk.c_str());
                    fprintf(vf, "    \"md5\": \"%s\",\n", e.md5.c_str());
                    fprintf(vf, "    \"offsets\": %zu,\n", e.count);
                    fprintf(vf, "    \"path\": \"%s\"%s\n", e.path.c_str(),
                            (e.st.empty() && e.luau.empty() && e.ff.empty()) ? "" : ",");
                    if (!e.st.empty())
                        fprintf(vf, "    \"struct\": \"%s\"%s\n", e.st.c_str(),
                                (e.luau.empty() && e.ff.empty()) ? "" : ",");
                    if (!e.luau.empty())
                        fprintf(vf, "    \"luau\": \"%s\"%s\n", e.luau.c_str(),
                                e.ff.empty() ? "" : ",");
                    if (!e.ff.empty())
                        fprintf(vf, "    \"fflags\": \"%s\"\n", e.ff.c_str());
                    fprintf(vf, "  }%s\n", i + 1 < ents.size() ? "," : "");
                }
                for (auto& o : other) fputs(o.c_str(), vf);
                fprintf(vf, "]\n");
                fclose(vf);
                LogOk("[publish] versions index updated (%zu entries)",
                      ents.size());
            }
        }
    }
    if (!ok) return;
    char msg[160];
    snprintf(msg, sizeof(msg), "Update offsets (%zu offsets, apk %s)",
             g_emitted.size(), apkVer.c_str());
    
    
    
    std::string files =
        "Offsets.hpp struct.hpp versions version/" + apkVer +
        "/Offsets.hpp version/" + apkVer + "/struct.hpp";
    std::string cmd = "git -C \"" + upDir + "\" diff --quiet -- " + files +
                      " 2>/dev/null || (git -C \"" + upDir + "\" add " +
                      files + " && git -C \"" + upDir + "\" commit -qm \"" +
                      msg + "\" && GIT_TERMINAL_PROMPT=0 git -C \"" + upDir +
                      "\" push 2>&1)";
    int rc = system(cmd.c_str());
    if (rc == 0)
        LogOk("[publish] website up to date / pushed");
    else
        LogBad("[publish] git push failed (rc=%d)", rc);
}

uintptr_t ScanU64(uintptr_t inst, uint64_t want, const char* name,
                  const char* cat, uintptr_t start, uintptr_t end, uintptr_t step) {
    for (uintptr_t i = start; i < end; i += step)
        if (Read<uint64_t>(inst + i) == want) {
            Emit(name, cat, i, "unsigned __int64");
            return i;
        }
    LogBad("MISS   %-28s %-14s (want %llu)", name, cat, (unsigned long long)want);
    return 0;
}

uintptr_t ScanFlt(uintptr_t inst, float want, const char* name,
                  const char* cat, uintptr_t start, uintptr_t end, float eps) {
    start &= ~3u;  
    for (uintptr_t i = start; i < end; i += 4) {
        float v = Read<float>(inst + i);
        if (!std::isnan(v) && fabsf(v - want) <= eps) {
            Emit(name, cat, i, "float");
            return i;
        }
    }
    LogBad("MISS   %-28s %-14s (want %f)", name, cat, want);
    return 0;
}

uintptr_t ScanDbl(uintptr_t inst, double want, const char* name,
                  const char* cat, double tol) {
    for (uintptr_t i = 0; i < 0x1000; i += 1) {
        double v = Read<double>(inst + i);
        if (v != 0 && fabs(1.0 / v - want) < tol) {
            Emit(name, cat, i, "double");
            return i;
        }
    }
    LogBad("MISS   %-28s %-14s", name, cat);
    return 0;
}

uintptr_t ScanDblX(uintptr_t inst, double want, const char* name,
                   const char* cat, double tol) {
    for (uintptr_t i = 0; i < 0x1000; i += 1) {
        double v = Read<double>(inst + i);
        if (v != 0 && fabs(v - want) < tol) {
            Emit(name, cat, i, "double");
            return i;
        }
    }
    LogBad("MISS   %-28s %-14s", name, cat);
    return 0;
}

uintptr_t ScanStr(uintptr_t inst, const std::string& want, const char* name,
                  const char* cat, uintptr_t end) {
    for (uintptr_t i = 0; i < end; i += 1) {
        if (ReadString(inst + i) == want) {
            Emit(name, cat, i, "string");
            return i;
        }
        
        char b[40];
        if (!ReadBytes(inst + i, b, sizeof(b))) continue;
        for (int sk = 0; sk < 3; ++sk) {
            int n = 0;
            while (n < 36 && b[sk + n] != 0) {
                if (b[sk + n] < 32 || b[sk + n] > 126) break;
                ++n;
            }
            if (n > 0 && b[sk + n] == 0 &&
                std::string(b + sk, n) == want) {
                Emit(name, cat, i + sk, "string");
                return i + sk;
            }
        }
    }
    LogBad("MISS   %-28s %-14s (want %s)", name, cat, want.c_str());
    return 0;
}

uintptr_t ScanI32(uintptr_t inst, int32_t want, const char* name,
                  const char* cat, uintptr_t start, uintptr_t end) {
    for (uintptr_t i = start; i < end; i += 1)
        if (Read<int32_t>(inst + i) == want) {
            Emit(name, cat, i, "int");
            return i;
        }
    LogBad("MISS   %-28s %-14s (want %d)", name, cat, want);
    return 0;
}

uintptr_t ScanBytes(uintptr_t inst, const char* want, size_t n,
                    const char* name, const char* cat, uintptr_t end) {
    char b[64];
    if (n > sizeof(b)) return 0;
    for (uintptr_t i = 0; i + n <= end; i += 1) {
        if (!ReadBytes(inst + i, b, n)) continue;
        if (memcmp(b, want, n) == 0) {
            Emit(name, cat, i, n == 1 ? "BYTE" : "bytes");
            return i;
        }
    }
    LogBad("MISS   %-28s %-14s (%zu bytes)", name, cat, n);
    return 0;
}

static bool Feq(float a, float b, float eps) {
    return !std::isnan(a) && fabsf(a - b) <= eps;
}

uintptr_t ScanVec3(uintptr_t inst, float x, float y, float z,
                   const char* name, const char* cat, float eps) {
    for (int pass = 0; pass < 2; ++pass) {
        uintptr_t step = pass == 0 ? 4 : 1;
        for (uintptr_t i = 0; i < 0x1000; i += step) {
            float v[3];
            if (!ReadBytes(inst + i, v, sizeof(v))) continue;
            if (Feq(v[0], x, eps) && Feq(v[1], y, eps) && Feq(v[2], z, eps)) {
                Emit(name, cat, i, "Vector3");
                return i;
            }
        }
    }
    LogBad("MISS   %-28s %-14s (want %g %g %g)", name, cat, x, y, z);
    return 0;
}

uintptr_t ScanVec2(uintptr_t inst, float x, float y, const char* name,
                   const char* cat, float eps) {
    for (uintptr_t i = 0; i < 0x1000; i += 4) {
        float v[2];
        if (!ReadBytes(inst + i, v, sizeof(v))) continue;
        if (Feq(v[0], x, eps) && Feq(v[1], y, eps)) {
            Emit(name, cat, i, "Vector2");
            return i;
        }
    }
    LogBad("MISS   %-28s %-14s (want %g %g)", name, cat, x, y);
    return 0;
}

uintptr_t ScanI16(uintptr_t inst, int16_t want, const char* name,
                  const char* cat) {
    for (uintptr_t i = 0; i < 0x1000; i += 2) {
        int16_t v = Read<int16_t>(inst + i);
        if (v == want) {
            Emit(name, cat, i, "short");
            return i;
        }
    }
    LogBad("MISS   %-28s %-14s (want %d)", name, cat, want);
    return 0;
}

uintptr_t ScanBoolM(const std::vector<uintptr_t>& insts,
                    const std::vector<int>& wants, const char* name,
                    const char* cat, uintptr_t start, uintptr_t end) {
    if (insts.empty() || insts.size() != wants.size()) {
        LogBad("MISS   %-28s %-14s (no instances)", name, cat);
        return 0;
    }
    
    
    
    for (int tier = 0; tier < 2; ++tier) {
        uintptr_t s = tier == 0 ? start : 0;
        uintptr_t e = tier == 0 ? end : 0x1000;
        for (uintptr_t i = s; i < e; i += 1) {
            bool all = true;
            for (size_t j = 0; j < insts.size(); ++j) {
                char b = Read<char>(insts[j] + i);
                if ((b != 0) != (wants[j] != 0)) {
                    all = false;
                    break;
                }
            }
            if (all) {
                Emit(name, cat, i, "bool");
                if (tier == 1)
                    LogInfo("WIDE   %-28s %-14s (outside flag cluster)",
                            name, cat);
                return i;
            }
        }
    }
    LogBad("MISS   %-28s %-14s (bool vector)", name, cat);
    return 0;
}

uintptr_t ScanIntM(const std::vector<uintptr_t>& insts,
                   const std::vector<int>& wants, const char* name,
                   const char* cat, uintptr_t start, uintptr_t end) {
    if (insts.empty() || insts.size() != wants.size()) {
        LogBad("MISS   %-28s %-14s (no instances)", name, cat);
        return 0;
    }
    for (int tier = 0; tier < 2; ++tier) {
        uintptr_t s = tier == 0 ? start : 0;
        uintptr_t e = tier == 0 ? end : 0x1000;
        for (uintptr_t i = s; i < e; i += 1) {
            bool all = true;
            for (size_t j = 0; j < insts.size(); ++j) {
                if (Read<int32_t>(insts[j] + i) != wants[j]) {
                    all = false;
                    break;
                }
            }
            if (all) {
                Emit(name, cat, i, "int");
                if (tier == 1)
                    LogInfo("WIDE   %-28s %-14s (outside flag cluster)",
                            name, cat);
                return i;
            }
        }
    }
    LogBad("MISS   %-28s %-14s (int vector)", name, cat);
    return 0;
}
