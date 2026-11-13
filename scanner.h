#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Emitted {
    std::string cat, name, ty;
    uintptr_t val;
};

inline std::vector<Emitted> g_emitted;

void Emit(const char* name, const char* cat, uintptr_t off, const char* ty);



bool Correct(const char* name, const char* cat, uintptr_t off);
void WriteHpp();
void WriteJson();
void WriteStruct();
void Publish();
uintptr_t ScanU64(uintptr_t inst, uint64_t want, const char* name,
                  const char* cat, uintptr_t start = 0,
                  uintptr_t end = 0x1000, uintptr_t step = 1);
uintptr_t ScanFlt(uintptr_t inst, float want, const char* name,
                  const char* cat, uintptr_t start = 0,
                  uintptr_t end = 0x1000, float eps = 0.001f);
uintptr_t ScanDbl(uintptr_t inst, double want, const char* name,
                  const char* cat, double tol);

uintptr_t ScanDblX(uintptr_t inst, double want, const char* name,
                   const char* cat, double tol);
uintptr_t ScanStr(uintptr_t inst, const std::string& want, const char* name,
                  const char* cat, uintptr_t end = 0x1000);
uintptr_t ScanI32(uintptr_t inst, int32_t want, const char* name,
                  const char* cat, uintptr_t start = 0,
                  uintptr_t end = 0x1000);
uintptr_t ScanI16(uintptr_t inst, int16_t want, const char* name,
                  const char* cat);

uintptr_t ScanBytes(uintptr_t inst, const char* want, size_t n,
                    const char* name, const char* cat,
                    uintptr_t end = 0x1000);
uintptr_t ScanVec3(uintptr_t inst, float x, float y, float z,
                   const char* name, const char* cat, float eps = 0.001f);
uintptr_t ScanVec2(uintptr_t inst, float x, float y, const char* name,
                   const char* cat, float eps = 0.001f);




uintptr_t ScanBoolM(const std::vector<uintptr_t>& insts,
                    const std::vector<int>& wants, const char* name,
                    const char* cat, uintptr_t start = 0x100,
                    uintptr_t end = 0x200);
uintptr_t ScanIntM(const std::vector<uintptr_t>& insts,
                   const std::vector<int>& wants, const char* name,
                   const char* cat, uintptr_t start = 0x100,
                   uintptr_t end = 0x200);
