#pragma once

#include <GfxRenderer.h>
#include <stdint.h>

// Decorative dividers: short landscape band, and either very wide or a reused asset.
bool ebookImageShouldInvert(GfxRenderer& renderer, int16_t width, int16_t height, const char* identityPath);

void ebookImageDarkModeResetSession();
