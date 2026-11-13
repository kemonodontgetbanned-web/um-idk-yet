#include "roblox.h"

#include <cstdint>
#include <string>
#include <vector>

#include "globals.h"
#include "mem.h"

std::vector<uintptr_t> GetChildren(uintptr_t inst) {
    std::vector<uintptr_t> out;
    uintptr_t holder = Read<uintptr_t>(inst + g_childrenStart);
    if (!IsValid(holder)) return out;
    uintptr_t start = Read<uintptr_t>(holder);
    uintptr_t end = Read<uintptr_t>(holder + 8);
    if (!IsValid(start) || !IsValid(end) || end < start) return out;
    if (end - start > 0x100000) return out;
    for (uintptr_t p = start; p < end; p += g_childStride) {
        uintptr_t c = Read<uintptr_t>(p);
        if (IsValid(c)) out.push_back(c);
    }
    return out;
}

std::string GetName(uintptr_t inst) {
    
    
    
    uintptr_t base = Read<uintptr_t>(inst + g_nameContainer);
    if (!IsValid(base)) return "invalid_str";
    char b[48];
    if (!ReadBytes(base, b, sizeof(b))) return "invalid_str";
    auto dec = [&](int skip) -> std::string {
        int n = 0;
        while (n < 40 && b[skip + n] != 0) {
            if (b[skip + n] < 32 || b[skip + n] > 126) break;
            ++n;
        }
        if (n >= 1 && n <= 40 && b[skip + n] == 0)
            return std::string(b + skip, n);
        return "";
    };
    std::string s9 = dec(9), s8 = dec(8);
    auto alpha0 = [](const std::string& s) {
        return !s.empty() && isalnum((unsigned char)s[0]);
    };
    if (alpha0(s9)) return s9;  
    if (alpha0(s8)) return s8;
    if (!s9.empty()) return s9;
    if (!s8.empty()) return s8;
    std::string s1 = dec(1);
    if (!s1.empty()) return s1;
    return dec(0).empty() ? "invalid_str" : dec(0);
}

uintptr_t FindFirstChild(uintptr_t inst, const std::string& name) {
    for (uintptr_t c : GetChildren(inst))
        if (GetName(c) == name) return c;
    return 0;
}
