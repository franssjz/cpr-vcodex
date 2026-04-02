#pragma once

#include "MappedInputManager.h"

namespace AutoTimeSync {

void noteReaderInteraction(const MappedInputManager& mappedInput);
void pollReaderSync();

}  // namespace AutoTimeSync
