#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace Dump {
bool Static();
bool General();
bool DataModel();
bool Players();
bool Camera();
bool DetectResolution();  
bool Humanoid();
bool Parts();
bool World();
bool Mouse();
bool Workspace();
bool Animation();
bool Lighting();
bool Interactables();

bool Luau();
void PublishLuau();

bool Flags();
void PublishFlags();


bool DiscoverName(uintptr_t& ncOut, uintptr_t& nOffOut);
bool DiscoverChildren();
bool UgcFind();
bool FindDm();
bool FindClass(int argc, char** argv);
bool FindDesc();
bool DataSlide();
bool RttiHist();
bool RttiFind();
bool Inspect(uintptr_t addr);
}  
