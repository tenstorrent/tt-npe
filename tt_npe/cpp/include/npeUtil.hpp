// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <fmt/core.h>
#include <stdio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <tuple>
#include <unordered_map>
#include <boost/container/small_vector.hpp>

#include "npeAssert.hpp"

namespace tt_npe {

inline bool is_tty_interactive() { return isatty(fileno(stdin)); }
inline bool enable_color() { return is_tty_interactive(); }

namespace TTYColorCodes {
inline const char *red = "\u001b[31m";
inline const char *green = "\u001b[32m";
inline const char *yellow = "\u001b[33m";
inline const char *gray = "\u001b[37m";
inline const char *reset = "\u001b[0m";
inline const char *clear_screen = "\033[2J\033[H";
inline const char *move_cursor_topleft = "\033[H";
inline const char *show_cursor = "\033[?25h";
inline const char *hide_cursor = "\033[?251";
inline const char *dark_bg = "\e[48;2;30;30;30m";
inline const char *bold = "\033[1m";
}  // namespace TTYColorCodes

template <typename... Args>
static void log_error(fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::println(
        stderr,
        "{}{}E: {}{}",
        TTYColorCodes::bold,
        TTYColorCodes::red,
        fmt::format(fmt, std::forward<Args>(args)...),
        TTYColorCodes::reset);
}
template <typename... Args>
static void log_warn(fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::println(
        stderr,
        "{}{}W: {}{}",
        TTYColorCodes::bold,
        TTYColorCodes::yellow,
        fmt::format(fmt, std::forward<Args>(args)...),
        TTYColorCodes::reset);
}
template <typename... Args>
static void log(fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::println(fmt, std::forward<Args>(args)...);
}

inline bool promptUser(const std::string &prompt_msg) {
    char response;
    fmt::print(
        stderr,
        "{}{}{} (y/n) : {}",
        TTYColorCodes::bold,
        TTYColorCodes::yellow,
        prompt_msg,
        TTYColorCodes::reset);
    std::cin >> response;
    fmt::println("");
    return tolower(response) == 'y' || tolower(response) == 'Y';
}

inline void printDiv(const std::string &title = "") {
    constexpr size_t total_width = 80;
    std::string padded_title = (title != "") ? std::string(" ") + title + " " : "";
    size_t bar_len = total_width - padded_title.size() - 4;
    std::string bar(bar_len, '-');
    fmt::println("\n--{}{}", padded_title, bar);
}

inline int64_t wrapToRange(int64_t number, int64_t range) {
    return ((number % range) + range) % range;
}

template <typename K, typename V, typename X>
inline const V &getWithDefault(
    const std::unordered_map<K, V> &container, const X &key, const V &default_val) {
    auto it = container.find(key);
    if (it == container.end()) {
        return default_val;
    } else {
        return it->second;
    }
}

// python-like enumerate wrapper for range based loops
// shamelessly stolen from : www.reedbeta.com/blog/python-like-enumerate-in-cpp17/
template <
    typename T,
    typename TIter = decltype(std::begin(std::declval<T>())),
    typename = decltype(std::end(std::declval<T>()))>
constexpr auto enumerate(T &&iterable) {
    struct iterator {
        size_t i;
        TIter iter;
        bool operator!=(const iterator &other) const { return iter != other.iter; }
        void operator++() {
            ++i;
            ++iter;
        }
        auto operator*() const { return std::tie(i, *iter); }
    };
    struct iterable_wrapper {
        T iterable;
        auto begin() { return iterator{0, std::begin(iterable)}; }
        auto end() { return iterator{0, std::end(iterable)}; }
    };
    return iterable_wrapper{std::forward<T>(iterable)};
}

struct Coord {
    Coord() : device_id(-1), row(-1), col(-1) {}
    Coord(DeviceID device_id, int row, int col) : device_id(device_id), row(row), col(col) {}
    bool operator==(const auto &rhs) const {
        return std::make_tuple(device_id, row, col) == std::make_tuple(rhs.device_id, rhs.row, rhs.col);
    }
    DeviceID device_id; 
    int16_t row, col;
};

struct MulticastCoordSet {
    // A pair of coords describing the 2D multicast target
    struct CoordGrid {
        Coord start_coord, end_coord;
        bool operator==(const auto &rhs) const {
            return std::make_tuple(
                       start_coord.row, start_coord.col, end_coord.row, end_coord.col) ==
                   std::make_tuple(
                       rhs.start_coord.row,
                       rhs.start_coord.col,
                       rhs.end_coord.row,
                       rhs.end_coord.col);
        }
    };
    using CoordGridContainer = boost::container::small_vector<CoordGrid, 1>;
    CoordGridContainer coord_grids;

    MulticastCoordSet() = default;
    MulticastCoordSet(const Coord &start, const Coord &end) {
        TT_ASSERT(start.device_id == end.device_id, "MulticastCoordSet: start and end coords must have the same device_id");
        TT_ASSERT(
            start.row <= end.row || start.col <= end.col,
            "MulticastCoordSet: start coord must be to the top-left of end coord");
        coord_grids.push_back(CoordGrid{start, end});
    }
    MulticastCoordSet(const CoordGridContainer &coord_grids) : coord_grids(coord_grids) {}

    MulticastCoordSet(const MulticastCoordSet &other) : coord_grids(other.coord_grids) {}

    bool operator==(const auto &rhs) const {
        if (coord_grids.size() != rhs.coord_grids.size())
            return false;
        for (size_t i = 0; i < coord_grids.size(); i++) {
            if (!(coord_grids[i] == rhs.coord_grids[i]))
                return false;
        }
        return true;
    }

    // create begin and end iterators over the bounding box formed by all CoordPairs
    struct iterator {
        iterator(const Coord &c, const MulticastCoordSet *m, size_t grid_idx = 0) :
            curr_iter_pos(c), mcast(m), current_grid_idx(grid_idx) {
            if (current_grid_idx >= mcast->coord_grids.size()) { 
                return; 
            }

            // Initialize with the first coordinate of the first pair
            curr_iter_pos = mcast->coord_grids[current_grid_idx].start_coord;
        }

        iterator &operator++() {
            // If we're at the end of all pairs, we're done
            if (current_grid_idx >= mcast->coord_grids.size())
                return *this;

            const auto &current_pair = mcast->coord_grids[current_grid_idx];

            // Move to the next column in the curr_iter_pos row
            if (curr_iter_pos.col < current_pair.end_coord.col) {
                curr_iter_pos.col++;
            } else {
                // Move to the next row, starting at the first column
                curr_iter_pos.col = current_pair.start_coord.col;
                curr_iter_pos.row++;

                // If we've gone past the last row of the current pair
                if (curr_iter_pos.row > current_pair.end_coord.row) {
                    // Move to the next pair
                    current_grid_idx++;

                    // If there are more pairs, start at the first coordinate of the next pair
                    if (current_grid_idx < mcast->coord_grids.size()) {
                        curr_iter_pos = mcast->coord_grids[current_grid_idx].start_coord;
                    }
                }
            }
            return *this;
        }
        bool operator!=(const iterator &other) const { 
            TT_ASSERT(mcast == other.mcast);
            // If we're at the end of all pairs or comparing with end iterator
            if (current_grid_idx >= mcast->coord_grids.size() || 
                other.current_grid_idx >= other.mcast->coord_grids.size()) {
                return current_grid_idx != other.current_grid_idx;
            }
            return curr_iter_pos != other.curr_iter_pos; 
        }

        Coord operator*() const { return curr_iter_pos; }

       private:
        Coord curr_iter_pos;
        const MulticastCoordSet *mcast;
        size_t current_grid_idx;  // Index of the current CoordPair in coord_grids
    };

    iterator begin() const {
        if (coord_grids.empty())
            return end();
        return iterator{coord_grids[0].start_coord, this, 0};
    }

    // Return one past the end iterator
    iterator end() const {
        // Create an iterator that's past the end of all pairs
        return iterator{Coord(), this, coord_grids.size()};
    }

    size_t grid_size() const {
        size_t total_size = 0;
        for (const auto &pair : coord_grids) {
            total_size += (pair.end_coord.row - pair.start_coord.row + 1) *
                          (pair.end_coord.col - pair.start_coord.col + 1);
        }
        return total_size;
    }
};

// Variant holding either a Coord (unicast) or MulticastCoordSet (multicast)
using NocDestination = std::variant<Coord,MulticastCoordSet>;

// taken from https://medium.com/@nerudaj/std-visit-is-awesome-heres-why-f183f6437932
// Allows writing nicer handling for std::variant types, like so:
// >  std::visit(overloaded{
// >    [&] (const TypeA&) { ... },
// >    [&] (const TypeB&) { ... }
// >  }, variant);
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

// Some compilers might require this explicit deduction guide
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

}  // namespace tt_npe

// specialize std::hash for Coord
namespace std {
template <>
struct hash<tt_npe::Coord> {
    size_t operator()(const tt_npe::Coord &c) const {
        size_t seed = 0xBAADF00DBAADF00D;
        seed ^= c.row + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= c.col + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// specialize std::hash for MulticastCoordSet
template <>
struct hash<tt_npe::MulticastCoordSet> {
    size_t operator()(const tt_npe::MulticastCoordSet &p) const {
        size_t seed = 0xBAADF00DBAADF00D;
        for (const auto &pair : p.coord_grids) {
            seed ^= std::hash<tt_npe::Coord>{}(pair.start_coord) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<tt_npe::Coord>{}(pair.end_coord) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

}  // namespace std

template <>
class fmt::formatter<tt_npe::Coord> {
   public:
    constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
    template <typename Context>
    constexpr auto format(tt_npe::Coord const &coord, Context &ctx) const {
        return format_to(ctx.out(), "({},{})", coord.row, coord.col);
    }
};

template <>
class fmt::formatter<tt_npe::MulticastCoordSet> {
   public:
    constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
    template <typename Context>
    constexpr auto format(tt_npe::MulticastCoordSet const &mcast_pair, Context &ctx) const {
        if (mcast_pair.coord_grids.empty()) {
            return format_to(ctx.out(), "(empty)");
        }
        
        auto out = ctx.out();
        for (size_t i = 0; i < mcast_pair.coord_grids.size(); ++i) {
            const auto &pair = mcast_pair.coord_grids[i];
            tt_npe::DeviceID device_id = pair.start_coord.device_id;
            out = format_to(out, "Dev{}({},{})-({},{})", 
                device_id, pair.start_coord.row, pair.start_coord.col, 
                pair.end_coord.row, pair.end_coord.col);
            
            // Add separator between pairs if not the last one
            if (i < mcast_pair.coord_grids.size() - 1) {
                out = format_to(out, ", ");
            }
        }
        return out;
    }
};
