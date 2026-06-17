#pragma once

#include <string>

#include "util/ShortcutRegistry.h"

namespace ShortcutUiMetadata {

std::string getSubtitle(const ShortcutDefinition& definition);
bool showAccessory(const ShortcutDefinition& definition);

}  // namespace ShortcutUiMetadata
