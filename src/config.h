#pragma once

#include <string>
#include <sys/types.h>

bool LoadConfig(const char* path);
bool LoadFpsXml();
pid_t FindMain();
