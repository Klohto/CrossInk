#include <gtest/gtest.h>

#include <random>
#include <string>

#include "src/activities/reader/TxtLineFit.h"

namespace {
int measure(const char* text) {
  int width = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
    if ((*p & 0xc0) != 0x80) width += *p == ' ' ? 2 : 3 + *p % 9;
  return width;
}

template <typename Measure>
size_t reference(const std::string& line, int maxWidth, Measure width) {
  size_t end = line.size();
  while (end && width(line.substr(0, end).c_str()) > maxWidth) {
    const size_t space = line.rfind(' ', end - 1);
    if (space != std::string::npos && space > 0)
      end = space;
    else {
      --end;
      while (end && txtLineFit::continuation(line[end])) --end;
    }
  }
  if (!end) {
    end = 1;
    while (end < line.size() && txtLineFit::continuation(line[end])) ++end;
  }
  return end;
}
}  // namespace

TEST(TxtLineFit, KeepsExistingBreaksForSpacesAndUtf8) {
  std::mt19937 random(167);
  const std::string tokens[] = {"a", "W", " ", "  ", "\t", "é", "中", "🙂", "Ω", "ب"};
  for (int sample = 0; sample < 1000; ++sample) {
    std::string line;
    for (int i = 0; i < 1 + int(random() % 150); ++i) line += tokens[random() % 10];
    for (int limit : {0, 1, 7, 40, 130, 400}) {
      if (measure(line.c_str()) <= limit) continue;
      auto copy = line;
      EXPECT_EQ(reference(line, limit, measure), txtLineFit::breakPosition(copy, limit, measure)) << line;
      EXPECT_EQ(copy, line);
    }
  }
}

TEST(TxtLineFit, LongParagraphUsesAtMostFourteenWidthQueries) {
  std::string line;
  for (int i = 0; i < 4096; ++i) line += "a ";
  int oldQueries = 0;
  const size_t expected = reference(line, 300, [&](const char* prefix) {
    ++oldQueries;
    return measure(prefix);
  });
  int newQueries = 0;
  EXPECT_EQ(txtLineFit::breakPosition(line, 300,
                                      [&](const char* prefix) {
                                        ++newQueries;
                                        return measure(prefix);
                                      }),
            expected);
  EXPECT_GT(oldQueries, 4000);
  EXPECT_LE(newQueries, 14);
}
