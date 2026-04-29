#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "logger.h"

// Redefine GamePlug macros to use the new Logger
// Logging is now handled via GamePlug::Logger::info(), etc.
#include "logger.h"
