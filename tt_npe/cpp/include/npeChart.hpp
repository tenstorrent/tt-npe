// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <vector>

/**
 * Displays a bar chart using ASCII drawing characters.
 * @param title The title of the bar chart
 * @param values The values to be displayed
 * @param width The width of the plot (default: 60)
 * @param height The height of the plot (default: 20)
 */
template <std::ranges::input_range RangeX, std::ranges::input_range RangeY>
    requires std::is_arithmetic_v<std::ranges::range_value_t<RangeX>> &&
             std::is_arithmetic_v<std::ranges::range_value_t<RangeY>>
void displayBarChart(
    const std::string& title,
    const RangeX& x_values,
    const RangeY& y_values,
    int width = 60,
    int height = 20) {
    // Ensure we have the same number of x and y values
    if (std::distance(x_values.begin(), x_values.end()) !=
        std::distance(y_values.begin(), y_values.end())) {
        fmt::println("Error: x_values and y_values must have the same size");
        return;
    }

    // Colors for the plot
    const std::string point_color = "\x1b[38;2;0;150;150m";
    const std::string axis_color = "\x1b[38;2;100;100;100m";
    const std::string reset_color = "\x1b[0m";

    // Find min and max values for scaling
    auto x_min = *std::min_element(x_values.begin(), x_values.end());
    auto x_max = *std::max_element(x_values.begin(), x_values.end());
    auto y_min = *std::min_element(y_values.begin(), y_values.end());
    auto y_max = *std::max_element(y_values.begin(), y_values.end());

    // Ensure we have a range to plot
    if (x_min == x_max)
        x_max = x_min + 1.0;
    if (y_min == y_max)
        y_max = y_min + 1.0;

    // Add a small margin to the ranges
    float x_margin = (x_max - x_min) * 0.05f;
    float y_margin = (y_max - y_min) * 0.05f;
    x_min -= x_margin;
    x_max += x_margin;
    y_min -= y_margin;
    y_max += y_margin;

    // Create a 2D grid for the scatter plot
    std::vector<std::vector<std::string>> grid(height, std::vector<std::string>(width, " "));

    // Draw axes if they are within the plot range
    int x_axis_row = height - 1 - static_cast<int>((0 - y_min) / (y_max - y_min) * (height - 1));
    int y_axis_col = static_cast<int>((0 - x_min) / (x_max - x_min) * (width - 1));

    // Ensure axes are within bounds
    x_axis_row = std::clamp(x_axis_row, 0, height - 1);
    y_axis_col = std::clamp(y_axis_col, 0, width - 1);

    // Draw x-axis
    for (int col = 0; col < width; col++) {
        if (x_axis_row >= 0 && x_axis_row < height) {
            grid[x_axis_row][col] = "-";
        }
    }

    // Draw y-axis
    for (int row = 0; row < height; row++) {
        if (y_axis_col >= 0 && y_axis_col < width) {
            grid[row][y_axis_col] = "|";
        }
    }

    // Mark the origin
    if (x_axis_row >= 0 && x_axis_row < height && y_axis_col >= 0 && y_axis_col < width) {
        grid[x_axis_row][y_axis_col] = "+";
    }

    // Plot the points as bars
    auto x_it = x_values.begin();
    auto y_it = y_values.begin();

    while (x_it != x_values.end() && y_it != y_values.end()) {
        float x = *x_it;
        float y = *y_it;

        // Convert data coordinates to grid coordinates
        int col = static_cast<int>((x - x_min) / (x_max - x_min) * (width - 1));
        // Calculate the height of the bar
        int bar_height = static_cast<int>((y - y_min) / (y_max - y_min) * (height - 1));

        // Draw the bar from x-axis (or bottom if x-axis not visible) up to the data point
        int start_row = height - 1;
        if (x_axis_row >= 0 && x_axis_row < height) {
            start_row = x_axis_row;
        }

        // Calculate the top of the bar
        int end_row = height - 1 - bar_height;
        end_row = std::max(0, end_row);  // Ensure it doesn't go beyond the top

        // Draw the bar
        if (col >= 0 && col < width) {
            for (int r = start_row - 1; r >= end_row; r--) {
                if (r >= 0 && r < height) {
                    grid[r][col] = "█";
                }
            }
        }

        ++x_it;
        ++y_it;
    }

    // Display the title
    fmt::println("\n{}", title);

    // Display the plot
    for (int row = 0; row < height; row++) {
        // Y-axis labels on the left side
        if (row == 0) {
            fmt::print("{}{:.2f}{} ", point_color, y_max, reset_color);
        } else if (row == height - 1) {
            fmt::print("{}{:.2f}{} ", point_color, y_min, reset_color);
        } else if (row == x_axis_row) {
            fmt::print("{}{:.2f}{} ", point_color, 0.0f, reset_color);
        } else {
            fmt::print("      ");
        }

        // Print the row
        for (int col = 0; col < width; col++) {
            if (grid[row][col] == "█") {
                fmt::print("{}{}{}", point_color, grid[row][col], reset_color);
            } else if (grid[row][col] != " ") {
                fmt::print("{}{}{}", axis_color, grid[row][col], reset_color);
            } else {
                fmt::print(" ");
            }
        }
        fmt::println("");
    }

    // Display x-axis labels
    fmt::print("      ");
    fmt::print("{}{:.2f}{}", point_color, x_min, reset_color);

    int mid_pos = width / 2 - 2;
    for (int i = 0; i < mid_pos - 5; i++) fmt::print(" ");

    if (x_min <= 0 && x_max >= 0) {
        fmt::print("{}{:.2f}{}", point_color, 0.0f, reset_color);
    } else {
        float mid_val = (x_min + x_max) / 2;
        fmt::print("{}{:.2f}{}", point_color, mid_val, reset_color);
    }

    for (int i = 0; i < mid_pos - 5; i++) fmt::print(" ");
    fmt::println("{}{:.2f}{}", point_color, x_max, reset_color);
}