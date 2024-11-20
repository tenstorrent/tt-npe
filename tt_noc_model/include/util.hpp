#pragma once

#include <cstddef>
#include <cstdint>
#include <fmt/core.h>

using CycleCount = uint32_t;

namespace tt_npe {

 // Overload for direct string formatting
template<typename... Args>
static void error(fmt::format_string<Args...> fmt, Args&&... args) {
    fmt::println("E: {}", fmt::format(fmt, std::forward<Args>(args)...));
}


inline void printDiv(const std::string &title = "") {
  constexpr size_t total_width = 80;
  size_t bar_len = total_width - title.size() - 4;
  std::string bar(bar_len, '-');
  fmt::println("\n-- {} {}", title, bar);
}

inline int64_t mapToRange(int64_t number, int64_t range) {
  return (number % range + range) % range;
}

struct Coord {
  int16_t row = 0, col = 0;
};

} // namespace tt_npe

template <> class fmt::formatter<tt_npe::Coord> {
public:
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  template <typename Context>
  constexpr auto format(tt_npe::Coord const &coord, Context &ctx) const {
    return format_to(ctx.out(), "({},{})", coord.row, coord.col);
  }
};
