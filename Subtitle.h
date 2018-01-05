#pragma once

#include <vector>
#include <string>
#include <utility>

struct Subtitle {
  template <typename T>
  Subtitle(int start, int stop, T&& text_lines)
  : start(start),
    stop(stop),
    text_lines(std::forward<T>(text_lines))
  {}

  int start;
  int stop;
  std::vector<std::string> text_lines;
};
