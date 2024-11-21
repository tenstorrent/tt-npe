#pragma once

#include <cstddef>
#include <cstdint>
#include <fmt/core.h>
#include <unordered_map>

using CycleCount = uint32_t;

namespace tt_npe {

// Overload for direct string formatting
template <typename... Args>
static void error(fmt::format_string<Args...> fmt, Args &&...args) {
  fmt::println("E: {}", fmt::format(fmt, std::forward<Args>(args)...));
}

inline void printDiv(const std::string &title = "") {
  constexpr size_t total_width = 80;
  std::string padded_title =
      (title != "") ? std::string(" ") + title + " " : "";
  size_t bar_len = total_width - padded_title.size() - 4;
  std::string bar(bar_len, '-');
  fmt::println("--{}{}", padded_title, bar);
}

inline int64_t wrapToRange(int64_t number, int64_t range) {
  return ((number % range) + range) % range;
}

template <typename K, typename V, typename X>
inline const V &getWithDefault(const std::unordered_map<K, V>& container, const X &key,
                    const V &default_val) {
  auto it = container.find(key);
  if (it == container.end()) {
    return default_val;
  } else {
    return it->second;
  }
}

struct Coord {
  int16_t row = 0, col = 0;
  bool operator ==(const auto& rhs) const {
    return std::make_pair(row,col) == std::make_pair(row,col);
  }
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
