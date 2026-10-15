#pragma once

#include "network/KOReaderRecovery.h"

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session. The live frontlight
// state rides along in the same flag so the reboot is invisible: the light
// comes back exactly as it was, regardless of the Restore Light on Wake
// preference.

void silentRestart();            // home screen
void silentRestartToReader();    // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToSettings();  // settings screen
// Reboot into the File Transfer > Join Network flow on a pristine heap, so the
// WiFi + TLS working set has the contiguous RAM it needs on tight boards. A
// no-op on touch boards (a soft reset would cycle their externally-powered
// rails); the caller then proceeds without a reboot.
void silentRestartToJoinNetwork();
void silentRestartToOta();

// One fresh-heap retry for an explicitly rejected KOReader TLS preflight.
// References a saved profile and never stores another copy of its credentials.
// Returns false if unsupported or the profile cannot be validated/persisted;
// a successful X3/X4 restart does not return.
bool silentRestartToKOReader(const KOReaderRecoveryRequest& request);

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
