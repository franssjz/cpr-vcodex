#include "CprVcodexLogs.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <string>

namespace {
constexpr char LOG_DIR[] = CPR_VCODEX_LOG_DIR;
constexpr char EVENTS_FILE[] = "/.crosspoint/cpr-vcodex-logs/boot_events.log";
RTC_NOINIT_ATTR uint32_t logSequence = 0;

uint32_t nextSequence() {
  if (logSequence > 1000000u) {
    logSequence = 0;
  }
  return ++logSequence;
}

void ensureLogDir() {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(LOG_DIR);
}

std::string buildReportPath(const char* prefix) {
  char buffer[96];
  snprintf(buffer, sizeof(buffer), "%s/%s_%06lu.txt", LOG_DIR, prefix, static_cast<unsigned long>(nextSequence()));
  return buffer;
}
}  // namespace

namespace CprVcodexLogs {

const char* getLogDir() { return LOG_DIR; }

void appendEvent(const char* category, const std::string& message) { appendEvent(category, message.c_str()); }

void appendEvent(const char* category, const char* message) {
  ensureLogDir();

  HalFile file = Storage.open(EVENTS_FILE, O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("LOG", "Could not open boot event log");
    return;
  }

  char prefix[48];
  snprintf(prefix, sizeof(prefix), "[%06lu] %s: ", static_cast<unsigned long>(nextSequence()),
           category ? category : "LOG");
  file.write(reinterpret_cast<const uint8_t*>(prefix), strlen(prefix));
  if (message && message[0] != '\0') {
    file.write(reinterpret_cast<const uint8_t*>(message), strlen(message));
  }
  file.write(static_cast<uint8_t>('\n'));
  file.flush();
  file.close();
}

bool writeReport(const char* prefix, const std::string& body, std::string* outPath) {
  ensureLogDir();
  const std::string path = buildReportPath(prefix && prefix[0] != '\0' ? prefix : "report");

  HalFile file;
  if (!Storage.openFileForWrite("LOG", path.c_str(), file)) {
    LOG_ERR("LOG", "Could not open report file for write: %s", path.c_str());
    return false;
  }

  const size_t written = file.write(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  file.flush();
  file.close();
  if (written != body.size()) {
    Storage.remove(path.c_str());
    LOG_ERR("LOG", "Short write while saving report: %s", path.c_str());
    return false;
  }

  if (outPath) {
    *outPath = path;
  }
  return true;
}

}  // namespace CprVcodexLogs
