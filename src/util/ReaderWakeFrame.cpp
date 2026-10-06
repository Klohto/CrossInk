#include "ReaderWakeFrame.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

namespace ReaderWakeFrame {
namespace {
constexpr char PATH[] = "/.crosspoint/reader_wake.bin";
constexpr char TEMP[] = "/.crosspoint/reader_wake.bin.part";
constexpr uint32_t MAGIC = 0x31574b52;  // RKW1
struct Header {
  uint32_t magic = MAGIC;
  uint32_t version = 2;
  uint32_t bookHash = 0;
  uint32_t bookBytes = 0;
  uint32_t bookModified = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t frameBytes = 0;
  uint32_t frameHash = 0;
  uint32_t orientation = 0;
  uint32_t footerPixels = 0;
  ReaderRaster::Key raster;
  char rasterPath[96] = {};
};
static_assert(sizeof(Header) <= 192);
bool savedThisBoot = false;
Header savedHeader;
bool restored = false;
bool restoredGray = false;
uint32_t restoredHash = 0;
uint32_t restoredContentHash = 0;
Header restoredHeader;

uint32_t contentHash(const uint8_t* frame, const Header& header) {
  const uint32_t stride = header.width / 8;
  uint32_t left = 0, right = header.width, top = 0, bottom = header.height;
  switch (header.orientation) {
    case GfxRenderer::Portrait:
      right -= header.footerPixels;
      break;
    case GfxRenderer::PortraitInverted:
      left += header.footerPixels;
      break;
    case GfxRenderer::LandscapeClockwise:
      top += header.footerPixels;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      bottom -= header.footerPixels;
      break;
  }
  uint32_t value = 2166136261U;
  for (uint32_t row = top; row < bottom; ++row) {
    for (uint32_t byte = left / 8; byte < (right + 7) / 8; ++byte) {
      uint8_t mask = 255;
      if (byte == left / 8) mask &= static_cast<uint8_t>(255U >> (left % 8));
      if (byte == right / 8 && right % 8) mask &= static_cast<uint8_t>(255U << (8 - right % 8));
      value = (value ^ (frame[row * stride + byte] & mask)) * 16777619U;
    }
  }
  return value;
}

bool bookStamp(const std::string& book, Header& header) {
  HalFile file;
  if (!Storage.openFileForRead("RKW", book, file)) return false;
  header.bookHash = ReaderRaster::hash(reinterpret_cast<const uint8_t*>(book.data()), book.size());
  header.bookBytes = file.size();
  header.bookModified = file.modificationTime();
  file.close();
  return true;
}

bool openFrame(const std::string& book, HalFile& file, Header& header) {
  Header expected;
  if (!bookStamp(book, expected) || !Storage.openFileForRead("RKW", PATH, file)) return false;
  if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != MAGIC || header.version != 2 ||
      header.bookHash != expected.bookHash || header.bookBytes != expected.bookBytes ||
      header.bookModified != expected.bookModified || !header.width || header.width % 8 || !header.height ||
      header.width > 4096 || header.height > 4096 || header.orientation > 3 ||
      header.footerPixels >= ((header.orientation % 2 == 0) ? header.width : header.height) ||
      header.frameBytes != header.width / 8 * header.height || file.size() != sizeof(header) + header.frameBytes ||
      !memchr(header.rasterPath, '\0', sizeof(header.rasterPath))) {
    file.close();
    return false;
  }
  return true;
}
}  // namespace

bool save(GfxRenderer& renderer, const std::string& book, const char* rasterPath, const ReaderRaster::Key* key,
          const uint32_t footerPixels) {
  if (!renderer.hasFrameBuffer()) return false;
  Header header;
  if (!bookStamp(book, header)) return false;
  header.width = renderer.getDisplayWidthBytes() * 8;
  header.height = renderer.getDisplayHeight();
  header.frameBytes = renderer.getBufferSize();
  header.frameHash = ReaderRaster::hash(renderer.getFrameBuffer(), header.frameBytes);
  header.orientation = renderer.getOrientation();
  header.footerPixels = footerPixels;
  if (key) header.raster = *key;
  if (rasterPath && key && strlen(rasterPath) < sizeof(header.rasterPath)) {
    memcpy(header.rasterPath, rasterPath, strlen(rasterPath) + 1);
  }
  if (savedThisBoot && memcmp(&header, &savedHeader, sizeof(header)) == 0 && Storage.exists(PATH)) return true;
  HalFile file;
  bool ok = Storage.openFileForWrite("RKW", TEMP, file);
  ok = ok && file.write(&header, sizeof(header)) == sizeof(header) &&
       file.write(renderer.getFrameBuffer(), header.frameBytes) == header.frameBytes && file.sync();
  file.close();
  if (ok) {
    if (Storage.exists(PATH)) ok = Storage.remove(PATH);
    ok = ok && Storage.rename(TEMP, PATH);
  }
  if (!ok) {
    LOG_ERR("RKW", "Could not save reader wake frame");
    Storage.remove(TEMP);
    Storage.remove(PATH);
    savedThisBoot = false;
    return false;
  }
  savedHeader = header;
  savedThisBoot = true;
  return true;
}

bool preflight(const std::string& book) {
  HalFile file;
  Header header;
  const bool ok = openFrame(book, file, header);
  file.close();
  return ok;
}

bool restore(GfxRenderer& renderer, const std::string& book) {
  restored = restoredGray = false;
  HalFile file;
  Header header;
  if (!openFrame(book, file, header)) return false;
  bool ok = header.frameBytes == renderer.getBufferSize() &&
            header.width == static_cast<uint32_t>(renderer.getDisplayWidthBytes() * 8) &&
            header.height == static_cast<uint32_t>(renderer.getDisplayHeight()) &&
            file.read(renderer.getFrameBuffer(), header.frameBytes) == static_cast<int>(header.frameBytes);
  file.close();
  ok = ok && ReaderRaster::hash(renderer.getFrameBuffer(), header.frameBytes) == header.frameHash;
  if (!ok) {
    Storage.remove(PATH);
    return false;
  }
  ReaderRaster raster;
  // One 24-row band for each plane: 7128 bytes on X3, released before fonts and
  // EPUB metadata load. Runtime panel geometry prevents a fixed stack buffer.
  const size_t capacity = static_cast<size_t>(renderer.getDisplayWidthBytes()) * ReaderRaster::BAND_ROWS;
  auto scratch = header.raster.gray && header.rasterPath[0] && renderer.supportsDirectGrayscale()
                     ? makeHeapByteBufferNoThrow(capacity * 3)
                     : HeapByteBuffer{};
  if (scratch && header.raster.width == header.width && header.raster.height == header.height && header.rasterPath[0] &&
      raster.open(header.rasterPath, header.raster)) {
    uint8_t* bw = scratch.get();
    uint8_t* low = bw + capacity;
    uint8_t* high = low + capacity;
    bool valid = true;
    while (valid && !raster.atEnd()) {
      uint16_t rows;
      valid = raster.readBand(bw, low, high, rows);
    }
    if (valid && raster.rewind() && renderer.beginDirectGrayscaleOverlay()) {
      int y = 0;
      while (valid && !raster.atEnd()) {
        uint16_t rows;
        valid = raster.readBand(bw, low, high, rows);
        if (!valid) break;
        const size_t bytes = static_cast<size_t>(renderer.getDisplayWidthBytes()) * rows;
        const uint8_t* base = renderer.getFrameBuffer() + static_cast<size_t>(y) * renderer.getDisplayWidthBytes();
        for (size_t i = 0; i < bytes; ++i) {
          const uint8_t changed = bw[i] ^ base[i];
          low[i] = (low[i] & static_cast<uint8_t>(~changed)) | (base[i] & changed);
          high[i] = (high[i] & static_cast<uint8_t>(~changed)) | (base[i] & changed);
        }
        renderer.writeGrayscalePlaneStrip(true, low, y, rows);
        renderer.writeGrayscalePlaneStrip(false, high, y, rows);
        y += rows;
      }
      if (valid) {
        renderer.displayGrayBuffer();
        restoredGray = true;
      }
    }
    raster.close();
  }
  if (!restoredGray) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  renderer.cleanupGrayscaleWithFrameBuffer();
  restoredHash = header.frameHash;
  restoredContentHash = contentHash(renderer.getFrameBuffer(), header);
  restoredHeader = header;
  restored = true;
  LOG_INF("RKW", "Reader visible before font load: gray=%u", restoredGray);
  return true;
}

bool consumeIfSame(const uint8_t* bw, const size_t bytes, const bool needsGray, const ReaderRaster::Key* key) {
  // Keep the saved footer until the next page turn. A bounded index may refine
  // its page-count estimate during wake; that change needs no second paint.
  const bool same = restored && (!key || *key == restoredHeader.raster) && (!needsGray || restoredGray) &&
                    bytes == restoredHeader.frameBytes &&
                    (ReaderRaster::hash(bw, bytes) == restoredHash ||
                     (key && restoredHeader.footerPixels && *key == restoredHeader.raster &&
                      contentHash(bw, restoredHeader) == restoredContentHash));
  restored = false;
  return same;
}
bool wasRestored() { return restored; }
void discard() {
  Storage.remove(PATH);
  Storage.remove(TEMP);
  savedThisBoot = false;
  restored = restoredGray = false;
}
}  // namespace ReaderWakeFrame
