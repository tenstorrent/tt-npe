// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#pragma once

#include <algorithm>
#include <fmt/core.h>
#include <ranges>
#include <stdio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <tuple>
#include <vector>
#include <boost/container/small_vector.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

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

template <std::integral T, std::integral U>
auto modulo(T n, U modulus) {
    auto result = n % modulus;
    return (result < 0) ? result + modulus : result;
}

template <typename K, typename V, typename X>
inline const V &getWithDefault(
    const boost::unordered_flat_map<K, V> &container, const X &key, const V &default_val) {
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

// Sorts the container and removes duplicate elements in-place.
// Requires the container to be mutable and support random access iterators
// (for std::sort) and forward iterators (for std::unique).
// The element type must support comparison (operator<).
// Constrained using C++20 concepts.
template <std::ranges::random_access_range Container>
    requires std::sortable<std::ranges::iterator_t<Container>>
void uniquify(Container& container) {
    std::sort(container.begin(), container.end());
    auto last = std::unique(container.begin(), container.end());
    container.erase(last, container.end());
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

    boost::container::small_vector<DeviceID, 2> getDeviceIDs() const {
        boost::container::small_vector<DeviceID, 2> device_ids;
        if (coord_grids.empty()) {
            return device_ids;
        }
        for (const auto& grid : coord_grids) {
            device_ids.push_back(grid.start_coord.device_id);
        }
        return device_ids;
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

using DeviceIDList = boost::container::small_vector<DeviceID, 2>;
inline DeviceIDList getDeviceIDsFromNocDestination(const NocDestination &destination) {
    return std::visit(overloaded{
        [&] (const Coord &c) { return DeviceIDList{c.device_id}; },
        [&] (const MulticastCoordSet &mcast) { return mcast.getDeviceIDs(); }
    }, destination);
}

enum class RiscType : uint8_t { BRISC, NCRISC, TRISC_0, TRISC_1, TRISC_2, ERISC, CORE_AGG };
enum class ZonePhase : uint8_t { ZONE_START, ZONE_END };

struct npeZone {
    double timestamp;
    std::string zone;
    ZonePhase zone_phase;
};

////////////////////////////////////////////////////
//             Hashing Related Functions          //
////////////////////////////////////////////////////

template <typename T>
T xorshift(const T &n, int i) {
    return n ^ (n >> i);
}

// a hash function with another name as to not confuse with std::hash
inline uint32_t distribute(const uint32_t &n) {
    uint32_t p = 0x55555555ul;  // pattern of alternating 0 and 1
    uint32_t c = 3423571495ul;  // random uneven integer constant;
    return c * xorshift(p * xorshift(n, 16), 16);
}

// a hash function with another name as to not confuse with std::hash
inline uint64_t distribute(const uint64_t &n) {
    uint64_t p = 0x5555555555555555ull;    // pattern of alternating 0 and 1
    uint64_t c = 17316035218449499591ull;  // random uneven integer constant;
    return c * xorshift(p * xorshift(n, 32), 32);
}

// call this function with the old seed and the new key to be hashed and combined into the new seed
// value, respectively the final hash
template <class T>
inline size_t hash_combine(std::size_t &seed, const T &v) {
    constexpr size_t ROTATION = std::numeric_limits<size_t>::digits / 3;
    return std::rotl(seed, ROTATION) ^ distribute(std::hash<T>{}(v));
}

// Generic container hashing function that iterates through any container
// and applies std::hash to each element, mixing the bits into the seed
template <typename Container>
inline size_t hash_container(size_t seed, const Container &container) {
    for (const auto &element : container) {
        seed = tt_npe::hash_combine(seed, element);
    }
    return seed;
}

}  // namespace tt_npe

// specialize std::hash for Coord
namespace std {
template <>
struct hash<tt_npe::Coord> {
    size_t operator()(const tt_npe::Coord &c) const {
        size_t seed = 0;
        seed = tt_npe::hash_combine(seed, c.device_id);
        seed = tt_npe::hash_combine(seed, c.row);
        seed = tt_npe::hash_combine(seed, c.col);
        return seed;
    }
};

// specialize std::hash for MulticastCoordSet::CoordGrid
template <>
struct hash<tt_npe::MulticastCoordSet::CoordGrid> {
    size_t operator()(const tt_npe::MulticastCoordSet::CoordGrid &grid) const {
        size_t seed = 0;
        seed = tt_npe::hash_combine(seed, grid.start_coord);
        seed = tt_npe::hash_combine(seed, grid.end_coord);
        return seed;
    }
};

// specialize std::hash for MulticastCoordSet
template <>
struct hash<tt_npe::MulticastCoordSet> {
    size_t operator()(const tt_npe::MulticastCoordSet &p) const {
        size_t seed = 0;
        // Use the hash_container function to hash all the coord_grids
        return tt_npe::hash_container(seed, p.coord_grids);
    }
};

}  // namespace std

// boost hash_value impl for custom types
namespace tt_npe {
inline std::size_t hash_value(tt_npe::Coord const &c) { return std::hash<tt_npe::Coord>{}(c); }

inline std::size_t hash_value(tt_npe::MulticastCoordSet::CoordGrid const &grid) {
    return std::hash<tt_npe::MulticastCoordSet::CoordGrid>{}(grid);
}

inline std::size_t hash_value(tt_npe::MulticastCoordSet const &p) {
    return std::hash<tt_npe::MulticastCoordSet>{}(p);
}
}  // namespace tt_npe

//////////////////////////////////////////////////////
//            fmt::formatter specializations        //
//////////////////////////////////////////////////////

template <>
class fmt::formatter<tt_npe::Coord> {
   public:
    constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
    template <typename Context>
    constexpr auto format(tt_npe::Coord const &coord, Context &ctx) const {
        return format_to(ctx.out(), "Dev{}({},{})", coord.device_id, coord.row, coord.col);
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

template <>
class fmt::formatter<tt_npe::NocDestination> {
   public:
    constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
    template <typename Context>
    constexpr auto format(tt_npe::NocDestination const &destination, Context &ctx) const {
        return std::visit(
            tt_npe::overloaded{
                [&](const tt_npe::Coord &c) { return format_to(ctx.out(), "{}", c); },
                [&](const tt_npe::MulticastCoordSet &mcast) {
                    return format_to(ctx.out(), "{}", mcast);
                }},
            destination);
    }
};
