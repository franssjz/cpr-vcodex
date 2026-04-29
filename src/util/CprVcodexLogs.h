#pragma once

#include <string>

#define CPR_VCODEX_LOG_DIR "/.crosspoint/cpr-vcodex-logs"

namespace CprVcodexLogs {

const char* getLogDir();

void appendEvent(const char* category, const char* message);
void appendEvent(const char* category, const std::string& message);

bool writeReport(const char* prefix, const std::string& body, std::string* outPath = nullptr);

}  // namespace CprVcodexLogs

#ifdef CPR_DISABLE_EVENT_LOGS
#define CPR_VCODEX_LOG_EVENT(category, message) \
  do {                                          \
  } while (false)
#define CPR_VCODEX_WRITE_REPORT(prefix, body, outPath) false
#else
#define CPR_VCODEX_LOG_EVENT(category, message) CprVcodexLogs::appendEvent(category, message)
#define CPR_VCODEX_WRITE_REPORT(prefix, body, outPath) CprVcodexLogs::writeReport(prefix, body, outPath)
#endif
