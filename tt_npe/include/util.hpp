#pragma once

#include <fmt/core.h>
#include <stdio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

using CycleCount = uint32_t;

namespace tt_npe {

inline bool is_tty_interactive() { return isatty(fileno(stdin)); }
inline bool enable_color() { return is_tty_interactive(); }

namespace TTYColorCodes {
inline const char *red = "\u001b[31m";
inline const char *yellow = "\u001b[33m";
inline const char *reset = "\u001b[0m";
}  // namespace TTYColorCodes

// Overload for direct string formatting
template <typename... Args>
static void log_error(fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::println(stderr,"{}E: {}{}", TTYColorCodes::red, fmt::format(fmt, std::forward<Args>(args)...), TTYColorCodes::reset);
}
template <typename... Args>
static void log_warn(fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::println(stderr,"{}W: {}{}", TTYColorCodes::yellow, fmt::format(fmt, std::forward<Args>(args)...), TTYColorCodes::reset);
}

inline void printDiv(const std::string &title = "") {
    constexpr size_t total_width = 80;
    std::string padded_title = (title != "") ? std::string(" ") + title + " " : "";
    size_t bar_len = total_width - padded_title.size() - 4;
    std::string bar(bar_len, '-');
    fmt::println("\n--{}{}", padded_title, bar);
}

inline int64_t wrapToRange(int64_t number, int64_t range) { return ((number % range) + range) % range; }

template <typename K, typename V, typename X>
inline const V &getWithDefault(const std::unordered_map<K, V> &container, const X &key, const V &default_val) {
    auto it = container.find(key);
    if (it == container.end()) {
        return default_val;
    } else {
        return it->second;
    }
}

struct Coord {
    int16_t row = 0, col = 0;
    bool operator==(const auto &rhs) const { return std::make_pair(row, col) == std::make_pair(row, col); }
};

}  // namespace tt_npe

template <>
class fmt::formatter<tt_npe::Coord> {
   public:
    constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
    template <typename Context>
    constexpr auto format(tt_npe::Coord const &coord, Context &ctx) const {
        return format_to(ctx.out(), "({},{})", coord.row, coord.col);
    }
};
