#pragma once

#include <cstddef>
#include <cstdint>

class GfxRenderer;
class Page;

namespace EpubGrayscale {
constexpr int GRAYSCALE_STRIP_ROWS = 80;

// Convert the reader's overlay masks to complete four-level planes. BW carries
// status text, solid shapes and clipping marks that have no gray mask.
inline void absoluteFromOverlay(const uint8_t* bw, uint8_t* lsb, uint8_t* msb, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t dark = lsb[i];
    lsb[i] = bw[i] | dark;
    msb[i] = bw[i] | (msb[i] & static_cast<uint8_t>(~dark));
  }
}

bool runDirectTextPass(GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop,
                       bool foregroundBlack, uint8_t* scratch, size_t scratchSize);

// Preserves the live BW buffer and existing controller synchronization. False
// leaves the caller responsible for its existing BW-snapshot fallback.
bool runTiledGrayscalePass(GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop,
                           bool foregroundBlack, bool needsTextGrayscale, bool needsImageGrayscale, uint8_t* scratch,
                           size_t scratchSize, bool asyncRefreshPending);
}  // namespace EpubGrayscale
