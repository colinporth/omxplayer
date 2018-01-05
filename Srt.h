#pragma once

#include <string>
#include <vector>

#include "Subtitle.h"

bool ReadSrt(const std::string& filename, std::vector<Subtitle>& subtitles);
