#pragma once

#include <Epub/ReaderRaster.h>

#include <cstddef>
#include <cstdint>
#include <string>

class GfxRenderer;

namespace ReaderWakeFrame {
bool save(GfxRenderer& renderer, const std::string& book, const char* rasterPath = nullptr,
          const ReaderRaster::Key* key = nullptr, uint32_t footerPixels = 0);
bool preflight(const std::string& book);
bool restore(GfxRenderer& renderer, const std::string& book);
bool consumeIfSame(const uint8_t* bw, size_t bytes, bool needsGray, const ReaderRaster::Key* key = nullptr);
bool wasRestored();
void discard();
}  // namespace ReaderWakeFrame
