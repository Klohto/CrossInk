#pragma once
struct HalDisplay {
  int width = 792, height = 528;
  int getDisplayWidth() const { return width; }
  int getDisplayHeight() const { return height; }
};
inline HalDisplay display;
