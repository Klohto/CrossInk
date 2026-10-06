#pragma once

#include <string>

// ASCII text uses nonnegative advances from one font. Shaping and CJK font
// fallback can change earlier advances when a prefix grows, so keep their
// existing search. Both paths preserve the stored TXT page offsets.
namespace txtLineFit {
inline bool continuation(char byte) { return (static_cast<unsigned char>(byte) & 0xc0) == 0x80; }

template <typename Measure>
size_t breakPosition(std::string& line, int maxWidth, Measure measure) {
  for (const unsigned char byte : line) {
    if (byte < 0x80) continue;
    size_t end = line.size();
    while (end && measure(line.substr(0, end).c_str()) > maxWidth) {
      const size_t space = line.rfind(' ', end - 1);
      if (space != std::string::npos && space > 0)
        end = space;
      else {
        --end;
        while (end && continuation(line[end])) --end;
      }
    }
    if (end) return end;
    end = 1;
    while (end < line.size() && continuation(line[end])) ++end;
    return end;
  }
  size_t low = 0;
  size_t high = line.size();
  while (low < high) {
    size_t end = low + (high - low + 1) / 2;
    while (end > low && end < line.size() && continuation(line[end])) --end;
    if (end == low) {
      ++end;
      while (end < line.size() && continuation(line[end])) ++end;
    }
    int width;
    if (end == line.size()) {
      width = measure(line.c_str());
    } else {
      const char saved = line[end];
      line[end] = '\0';
      width = measure(line.c_str());
      line[end] = saved;
    }
    if (width <= maxWidth) {
      low = end;
    } else {
      high = end - 1;
      while (high > low && continuation(line[high])) --high;
    }
  }
  const size_t space = line.rfind(' ', low);
  if (space != std::string::npos && space > 0) return space;
  if (low) return low;
  size_t end = 1;
  while (end < line.size() && continuation(line[end])) ++end;
  return end;
}
}  // namespace txtLineFit
