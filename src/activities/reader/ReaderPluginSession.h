#pragma once

#include <atomic>
#include <string>

#include "ReaderSession.h"

// Plugin session reporting is separate from the fork's persistent reading statistics.
// The reader holds RenderLock for turns, rendering, flush and exit; open is loop-only.
class ReaderPluginSession {
 public:
  void noteTurn(bool forward, bool succeeded) { session.noteTurn(forward, succeeded); }
  void rendered(int progressBp);
  void openOnceRendered(const std::string& path);
  void flush(const std::string& path);
  void exit(const std::string& path);

 private:
  ReaderSession session;
  std::atomic<bool> pageRendered{false};
  bool opened = false;
  int progress = 0;
};
