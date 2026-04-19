#include "PngSleepRenderer.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PNGdec.h>

#include <new>
#include <string>

namespace {

constexpr size_t PNG_DECODER_APPROX_SIZE = 44 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_PNG = PNG_DECODER_APPROX_SIZE + 16 * 1024;

struct PngOverlayCtx {
  const GfxRenderer* renderer;
  int screenW;
  int screenH;
  int srcWidth;
  int dstWidth;
  int dstX;
  int dstY;
  float yScale;
  int lastDstY;
  int32_t transparentColor;
  PNG* pngObj;
};

void* pngSleepOpen(const char* filename, int32_t* size) {
  FsFile* f = new FsFile();
  if (!Storage.openFileForRead("SLP", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void pngSleepClose(void* handle) {
  auto* f = reinterpret_cast<FsFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

int32_t pngSleepRead(PNGFILE* pFile, uint8_t* pBuf, int32_t len) {
  auto* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  return f ? f->read(pBuf, len) : 0;
}

int32_t pngSleepSeek(PNGFILE* pFile, int32_t pos) {
  auto* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(pos);
}

int pngOverlayDraw(PNGDRAW* pDraw) {
  auto* ctx = reinterpret_cast<PngOverlayCtx*>(pDraw->pUser);
  if (!ctx) return 0;

  if (ctx->transparentColor == -2) {
    const int pixelType = pDraw->iPixelType;
    ctx->transparentColor = (pDraw->iHasAlpha &&
                             (pixelType == PNG_PIXEL_TRUECOLOR || pixelType == PNG_PIXEL_GRAYSCALE))
                                ? static_cast<int32_t>(ctx->pngObj->getTransparentColor())
                                : -1;
  }

  const int destY = ctx->dstY + static_cast<int>(pDraw->y * ctx->yScale);
  if (destY == ctx->lastDstY) return 1;
  ctx->lastDstY = destY;
  if (destY < 0 || destY >= ctx->screenH) return 1;

  const uint8_t* pixels = pDraw->pPixels;
  const int pixelType = pDraw->iPixelType;
  const int hasAlpha = pDraw->iHasAlpha;

  int srcX = 0;
  int error = 0;
  for (int dstX = 0; dstX < ctx->dstWidth; dstX++) {
    const int outX = ctx->dstX + dstX;
    if (outX >= 0 && outX < ctx->screenW) {
      uint8_t alpha = 255;
      uint8_t gray = 0;

      switch (pixelType) {
        case PNG_PIXEL_TRUECOLOR_ALPHA: {
          const uint8_t* p = &pixels[srcX * 4];
          alpha = p[3];
          gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          break;
        }
        case PNG_PIXEL_GRAY_ALPHA:
          gray = pixels[srcX * 2];
          alpha = pixels[srcX * 2 + 1];
          break;
        case PNG_PIXEL_TRUECOLOR: {
          const uint8_t* p = &pixels[srcX * 3];
          gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          if (ctx->transparentColor >= 0 && p[0] == static_cast<uint8_t>((ctx->transparentColor >> 16) & 0xFF) &&
              p[1] == static_cast<uint8_t>((ctx->transparentColor >> 8) & 0xFF) &&
              p[2] == static_cast<uint8_t>(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        }
        case PNG_PIXEL_GRAYSCALE:
          gray = pixels[srcX];
          if (ctx->transparentColor >= 0 && gray == static_cast<uint8_t>(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        case PNG_PIXEL_INDEXED:
          if (pDraw->pPalette) {
            const uint8_t idx = pixels[srcX];
            const uint8_t* p = &pDraw->pPalette[idx * 3];
            gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            if (hasAlpha) alpha = pDraw->pPalette[768 + idx];
          }
          break;
        default:
          gray = pixels[srcX];
          break;
      }

      if (alpha >= 128) {
        ctx->renderer->drawPixel(outX, destY, gray < 128);
      }
    }

    error += ctx->srcWidth;
    while (error >= ctx->dstWidth) {
      error -= ctx->dstWidth;
      srcX++;
    }
  }

  return 1;
}

}  // namespace

bool PngSleepRenderer::drawTransparentPng(const std::string& path, const GfxRenderer& renderer, const int targetX,
                                          const int targetY, const int targetWidth, const int targetHeight) {
  if (targetWidth <= 0 || targetHeight <= 0) {
    return false;
  }

  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_PNG) {
    LOG_ERR("SLP", "Not enough heap for PNG sleep image");
    return false;
  }

  PNG* png = new (std::nothrow) PNG();
  if (!png) {
    return false;
  }

  int rc = png->open(path.c_str(), pngSleepOpen, pngSleepClose, pngSleepRead, pngSleepSeek, pngOverlayDraw);
  if (rc != PNG_SUCCESS) {
    LOG_ERR("SLP", "PNG open failed: %s (%d)", path.c_str(), rc);
    delete png;
    return false;
  }

  const int srcW = png->getWidth();
  const int srcH = png->getHeight();
  int dstW = srcW;
  int dstH = srcH;
  float yScale = 1.0f;

  if (srcW > targetWidth || srcH > targetHeight) {
    const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(srcW);
    const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(srcH);
    const float scale = (scaleX < scaleY) ? scaleX : scaleY;
    dstW = static_cast<int>(srcW * scale);
    dstH = static_cast<int>(srcH * scale);
    yScale = static_cast<float>(dstH) / static_cast<float>(srcH);
  }

  PngOverlayCtx ctx{
      &renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), srcW, dstW, targetX + (targetWidth - dstW) / 2,
      targetY + (targetHeight - dstH) / 2, yScale, -1, -2, png};

  rc = png->decode(&ctx, 0);
  png->close();
  delete png;
  return rc == PNG_SUCCESS;
}
