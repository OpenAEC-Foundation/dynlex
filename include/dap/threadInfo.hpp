#pragma once

#include <string>

namespace tbx {

// Thread information (3BX is single-threaded for now)
struct ThreadInfo {
  int id{1};
  std::string name{"main"};
};

} // namespace tbx
