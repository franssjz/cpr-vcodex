#pragma once

#include "CrossPointState.h"

namespace KOReaderSyncRecovery {
// Only a pending manual handoff may survive a low-memory restart. Completed
// outcomes belong to the reader, and automatic sync must never restart it.
inline bool canResume(const KOReaderSyncSessionState& sync) {
  return sync.active && sync.outcome == KOReaderSyncOutcomeState::PENDING && !sync.epubPath.empty() &&
         !sync.exitToHomeAfterSync && sync.autoPullEpubPath.empty() &&
         (sync.intent == KOReaderSyncIntentState::COMPARE || sync.intent == KOReaderSyncIntentState::PULL_REMOTE ||
          sync.intent == KOReaderSyncIntentState::PUSH_LOCAL);
}
}  // namespace KOReaderSyncRecovery
