// Mock Logging.h for native testing - all logging macros are no-ops
#pragma once

#include <string>

#define LOG_DBG(origin, format, ...)
#define LOG_ERR(origin, format, ...)
#define LOG_INF(origin, format, ...)

inline std::string getLastLogs() { return ""; }
inline void clearLastLogs() {}
inline bool sanitizeLogHead() { return false; }
