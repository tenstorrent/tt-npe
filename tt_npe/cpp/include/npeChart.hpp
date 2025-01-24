#include <fmt/core.h>
#include <concepts>

template <std::ranges::input_range Range>
requires std::is_arithmetic_v<std::ranges::range_value_t<Range>>
void displayBarChart(const std::string& title, const Range &values) {
    const std::string bar_color = "\x1b[38;2;0;0;150m";
    const std::string reset_color = "\x1b[0m";

    auto max_value = *std::max_element(values.begin(), values.end());
    float bar_scale = 80.f / max_value;

    fmt::println("{}", title);
    int i = 0;
    for (const auto &value : values) {
        std::string bar;
        for (int j = 0; j < bar_scale * value; j++) {
            bar.append("█");
        }
        bar.append(fmt::format(" {:.1f}", value));
        fmt::println("{:3d}|{}{}{}", i++, bar_color, bar, reset_color);
    }
}