#pragma once

#include <climits>
#include <cstddef>
#include <cstring>

namespace OtaReleasePolicy {
inline constexpr char latestReleaseUrl[] = "https://api.github.com/repos/Klohto/CrossInk/releases/latest";

struct Version {
  int numbers[6]{};
  bool valid = false;
  bool candidate = false;
};

inline bool number(const char*& text, int& value) {
  if (*text < '0' || *text > '9') return false;
  value = 0;
  while (*text >= '0' && *text <= '9') {
    const int digit = *text++ - '0';
    if (value > (INT_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  return true;
}

inline Version parse(const char* text) {
  Version result;
  if (!text) return result;
  if (*text == 'v' || *text == 'V') ++text;
  for (size_t part = 0; part < 4; ++part) {
    if (!number(text, result.numbers[part])) return result;
    if (*text != '.') break;
    if (part == 3) return result;
    ++text;
  }
  if (*text == '-' && text[1] >= '0' && text[1] <= '9') {
    ++text;
    if (!number(text, result.numbers[4])) return result;
    if (*text == '.') {
      ++text;
      if (!number(text, result.numbers[5])) return result;
    }
    if (*text == '.') return result;
  }
  if (*text && *text != '-' && *text != '+') return result;
  for (const char* p = text; p[0] && p[1] && p[2]; ++p) {
    if (p[0] == '-' && (p[1] == 'r' || p[1] == 'R') && (p[2] == 'c' || p[2] == 'C')) {
      result.candidate = true;
    }
  }
  result.valid = true;
  return result;
}

inline int compare(const char* latestText, const char* currentText) {
  const Version latest = parse(latestText), current = parse(currentText);
  if (!latest.valid || !current.valid) return 0;
  for (size_t part = 0; part < 6; ++part) {
    if (latest.numbers[part] != current.numbers[part]) {
      return latest.numbers[part] > current.numbers[part] ? 1 : -1;
    }
  }
  if (latest.candidate != current.candidate) return latest.candidate ? -1 : 1;
  return 0;
}

inline bool firmwareAssetMatches(const char* name, const char* device) {
  if (!name || !device) return false;
  const size_t length = std::strlen(name);
  if (length < 4 || std::strcmp(name + length - 4, ".bin") != 0) return false;
  if (std::strcmp(device, "x3-x4") == 0 && std::strncmp(name, "crossink-", 9) == 0) {
    return length > 16 && std::strcmp(name + length - 7, "-x3.bin") == 0;
  }
  constexpr char prefix[] = "firmware-";
  const size_t deviceLength = std::strlen(device);
  if (std::strncmp(name, prefix, sizeof(prefix) - 1) != 0) return false;
  const char* suffix = name + sizeof(prefix) - 1;
  if (std::strncmp(suffix, device, deviceLength) != 0) return false;
  suffix += deviceLength;
  return std::strcmp(suffix, ".bin") == 0 || (*suffix == '-' && std::strlen(suffix) > 5);
}
}  // namespace OtaReleasePolicy
