// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <cassert>
#include <vector>

namespace tt_npe {

template <typename T>
class Grid2D {
   private:
    std::vector<T> data;
    size_t kRows;
    size_t kCols;

   public:
    Grid2D() : kRows(0), kCols(0) {}
    Grid2D(size_t num_rows, size_t num_cols, const T &default_value = T()) :
        data(num_rows * num_cols, default_value), kRows(num_rows), kCols(num_cols) {}

    // returns true if row and col are in bounds
    bool inBounds(size_t row, size_t col) const { return row < kRows && col < kCols; }

    // returns ref to elem at [row][col]
    T &operator()(size_t row, size_t col) {
        assert(inBounds(row, col));
        return data[row * kCols + col];
    }

    // returns const ref to elem at [row][col]
    const T &operator()(size_t row, size_t col) const {
        assert(inBounds(row, col));
        return data[row * kCols + col];
    }
    void reset(const T &clear_val = 0.0f) { std::fill(data.begin(), data.end(), clear_val); }

    // Get dimensions
    size_t getRows() const { return kRows; }
    size_t getCols() const { return kCols; }
    size_t size() const { return kRows * kCols; }

    // iterators
    typename std::vector<T>::iterator begin() { return data.begin(); }
    typename std::vector<T>::iterator end() { return data.end(); }
    typename std::vector<T>::const_iterator begin() const { return data.begin(); }
    typename std::vector<T>::const_iterator end() const { return data.end(); }
};

template <typename T>
class Grid3D {
   private:
    std::vector<T> data;
    size_t kRows;
    size_t kCols;
    size_t kItems;
    size_t kRowSize;  // Precomputed factor

   public:
    Grid3D() : kRows(0), kCols(0), kItems(0), kRowSize(0) {}

    Grid3D(size_t num_rows, size_t num_cols, size_t num_items, const T &default_value = T()) :
        data(num_rows * num_cols * num_items, default_value),
        kRows(num_rows),
        kCols(num_cols),
        kItems(num_items),
        kRowSize(num_cols * num_items)  // Precompute this factor
    {}

    // returns true if row, col, and item are in bounds
    bool inBounds(size_t row, size_t col, size_t item) const {
        return row < kRows && col < kCols && item < kItems;
    }

    // returns ref to elem at [row][col][item]
    T &operator()(size_t row, size_t col, size_t item) {
        assert(inBounds(row, col, item));
        return data[row * kRowSize + col * kItems + item];
    }

    // returns const ref to elem at [row][col][item]
    const T &operator()(size_t row, size_t col, size_t item) const {
        assert(inBounds(row, col, item));
        return data[row * kRowSize + col * kItems + item];
    }
    void reset(const T &clear_val = 0.0f) { std::fill(data.begin(), data.end(), clear_val); }

    // Get dimensions
    size_t getRows() const { return kRows; }
    size_t getCols() const { return kCols; }
    size_t getItems() const { return kItems; }
    size_t size() const { return kRows * kCols * kItems; }

    // iterators
    typename std::vector<T>::iterator begin() { return data.begin(); }
    typename std::vector<T>::iterator end() { return data.end(); }
    typename std::vector<T>::const_iterator begin() const { return data.begin(); }
    typename std::vector<T>::const_iterator end() const { return data.end(); }
};

using LinkDemandGrid = std::vector<float>;
using NIUDemandGrid = std::vector<float>;

}  // namespace tt_npe
