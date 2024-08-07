#pragma once
#include "../OpenOVR/Drivers/Backend.h"

// Since we're not importing stdafx, add another nasty little hack
#ifndef _WIN32
#include "../OpenOVR/linux_funcs.h"
#endif

namespace DrvOpenXR {
IBackend* CreateOpenXRBackend(const char* startupInfo);
void GetXRAppName(char (&appName)[128]);
}; // namespace DrvOpenXR
