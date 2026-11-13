#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>

inline uint64_t g_placeId = 113264082193197ull;
inline uint64_t g_gameId = 8220026920ull;
inline uint64_t g_creatorId = 8979812863ull;
inline uint64_t g_userId = 1055818798ull;
inline std::string g_displayName = "idom";
inline std::string g_localeId = "GB";  

inline float g_fHealth = 52.382f, g_fMaxHealth = 88.817f, g_fWalk = 18.827f;
inline float g_fJumpPower = 27.322f, g_fJumpHeight = 7.812f, g_fHip = 1.998f;
inline float g_fSlope = 89.9f;

inline float g_camX = 0.f, g_camY = 7.33f, g_camZ = 12.074f;
inline float g_camPitch = 15.0f, g_fov = 1.3166f;
inline float g_minZoom = 0.528f, g_maxZoom = 128.f;
inline int g_camMode = 0, g_camType = 5, g_winW = 1920, g_winH = 1080;
inline int g_teamColor = 1015;  
inline std::string g_dumpedPath;  
inline std::string g_exeDir = ".";
inline double g_maxFps = 144.0;

inline pid_t g_pid = -1;


inline uintptr_t g_nameContainer = 0x70, g_nameOff = 0x8;
inline uintptr_t g_childrenStart = 0x78;
inline uintptr_t g_childStride = 16;  
inline uintptr_t g_dataModel = 0;
