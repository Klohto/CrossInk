#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace HalDisplay {
enum RefreshMode { HALF_REFRESH };
}
class GfxRenderer {
 public:
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation orientation = LandscapeCounterClockwise;
  Orientation getOrientation() const { return orientation; }
  std::vector<uint8_t> frame = std::vector<uint8_t>(8 * 35, 255);
  std::vector<uint8_t> low, high;
  bool direct = true;
  unsigned grayRefreshes = 0, bwRefreshes = 0, cleanups = 0;
  bool hasFrameBuffer() const { return !frame.empty(); }
  int getDisplayWidthBytes() const { return 8; }
  int getDisplayHeight() const { return 35; }
  size_t getBufferSize() const { return frame.size(); }
  uint8_t* getFrameBuffer() { return frame.data(); }
  bool supportsDirectGrayscale() const { return direct; }
  bool beginDirectGrayscaleOverlay() {
    low.resize(frame.size());
    high.resize(frame.size());
    return direct;
  }
  void writeGrayscalePlaneStrip(bool lsb, const uint8_t* data, int y, int rows) {
    auto& target = lsb ? low : high;
    std::copy_n(data, rows * 8, target.data() + y * 8);
  }
  void displayGrayBuffer() { ++grayRefreshes; }
  void displayBuffer(HalDisplay::RefreshMode) { ++bwRefreshes; }
  void cleanupGrayscaleWithFrameBuffer() { ++cleanups; }
};
