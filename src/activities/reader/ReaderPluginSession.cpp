#include "ReaderPluginSession.h"

#include <Arduino.h>
#include <KOReaderDocumentId.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cstdio>

#include "util/PluginEvents.h"

void ReaderPluginSession::rendered(const int progressBp) {
  progress = std::clamp(progressBp, 0, 10000);
  session.onRenderComplete(millis(), trustedtime::trustedNow(), progress);
  pageRendered.store(true, std::memory_order_release);
}

void ReaderPluginSession::openOnceRendered(const std::string& path) {
  if (opened || !pageRendered.load(std::memory_order_acquire)) return;
  opened = true;
  const pluginevents::Var vars[] = {{"book", path.c_str()}};
  pluginevents::emit(pluginevents::Event::ReaderOpen, vars, 1);
}

void ReaderPluginSession::flush(const std::string& path) {
  openOnceRendered(path);
  if (session.isEmitWorthy() && pluginevents::anySubscriber(pluginevents::Event::ReaderSession)) {
    const std::string document = KOReaderDocumentId::calculate(path);
    const bool valid = document.size() == 32 && std::all_of(document.begin(), document.end(), [](unsigned char c) {
                         return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                       });
    if (valid) {
      char start[24], end[24], duration[16], startProgress[8], endProgress[8];
      snprintf(start, sizeof(start), "%lld", static_cast<long long>(session.startTime()));
      snprintf(end, sizeof(end), "%lld", static_cast<long long>(session.endTime()));
      snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(session.durationSeconds()));
      snprintf(startProgress, sizeof(startProgress), "%u", session.startProgressBp());
      snprintf(endProgress, sizeof(endProgress), "%u", session.endProgressBp());
      const pluginevents::Var vars[] = {{"book", path.c_str()},
                                        {"document", document.c_str()},
                                        {"start_time", start},
                                        {"end_time", end},
                                        {"duration_seconds", duration},
                                        {"start_progress_bp", startProgress},
                                        {"end_progress_bp", endProgress},
                                        {"progress_scale", "10000"}};
      pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8);
    }
  }
  session.reset();
}

void ReaderPluginSession::exit(const std::string& path) {
  flush(path);
  if (!opened) return;
  char percent[8];
  snprintf(percent, sizeof(percent), "%d", progress / 100);
  const pluginevents::Var vars[] = {{"book", path.c_str()}, {"percent", percent}};
  pluginevents::emit(pluginevents::Event::ReaderExit, vars, 2);
  opened = false;
  pageRendered.store(false, std::memory_order_release);
}
