#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::vector<uintptr_t> GetChildren(uintptr_t inst);
std::string GetName(uintptr_t inst);
uintptr_t FindFirstChild(uintptr_t inst, const std::string& name);
