#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Snap {
    uintptr_t base = 0, size = 0;  
    std::vector<char> buf;
};

inline std::vector<Snap> g_snaps;        
inline std::vector<uintptr_t> g_rxbases;  
inline uintptr_t g_rxActive = 0;
inline std::string g_libPath = "/home/idom/libroblox.so";

uint64_t L64(uintptr_t addr);
std::string DemangleItanium(const std::string& m);
std::string RttiGet(uintptr_t obj);
bool SnapshotData();
uintptr_t ActiveRx();
