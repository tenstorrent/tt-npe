// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "npeDeviceModelConfig.hpp"

namespace tt_npe {
namespace {

std::filesystem::path blackholeModelConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
           "data/device/models/blackhole.yaml";
}

class TemporaryYaml {
   public:
    explicit TemporaryYaml(std::string_view contents) {
        static std::atomic<uint64_t> sequence = 0;
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                fmt::format(
                    "tt_npe_device_model_config_{}_{}.yaml",
                    timestamp,
                    sequence.fetch_add(1));

        std::ofstream output(path_);
        output << contents;
    }

    ~TemporaryYaml() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

   private:
    std::filesystem::path path_;
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

TEST(npeDeviceModelConfigTest, LoadsBlackholeConfig) {
    const auto config = parseNpeDeviceModelConfig(blackholeModelConfigPath());

    EXPECT_FLOAT_EQ(config.link_bandwidth, 60.9f);
    EXPECT_FLOAT_EQ(config.eth_bandwidth_per_link, 50.0f);
    EXPECT_EQ(config.dram_channels_per_controller, 1);

    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::WORKER), 60.9f);
    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::DRAM), 40.0f);
    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::ETH), 999.9f);
    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::UNDEF), 60.9f);
    EXPECT_EQ(config.injection_rates, config.absorption_rates);

    const TransferBandwidthTable expected_table = {
        {0, 0.0f},
        {128, 6.0f},
        {256, 12.1f},
        {512, 24.2f},
        {1024, 48.0f},
        {2048, 57.7f},
        {4096, 58.7f},
        {8192, 60.4f},
        {16384, 60.9f}};
    EXPECT_EQ(config.transfer_bandwidth_table, expected_table);

    EXPECT_EQ(config.read_latencies.same_tile, 65);
    EXPECT_EQ(config.read_latencies.same_col, 177);
    EXPECT_EQ(config.read_latencies.same_row, 217);
    EXPECT_EQ(config.read_latencies.diagonal, 329);
    EXPECT_EQ(config.write_latencies.startup, 40);
    EXPECT_EQ(config.write_latencies.cycles_per_hop, 11);
}

TEST(npeDeviceModelConfigTest, RejectsMissingRequiredField) {
    const TemporaryYaml invalid_yaml("eth_bandwidth_per_link: 50.0\n");

    EXPECT_THROW(parseNpeDeviceModelConfig(invalid_yaml.path()), npeException);
}

TEST(npeDeviceModelConfigTest, RejectsUnsortedTransferBandwidthTable) {
    auto yaml = readFile(blackholeModelConfigPath());
    constexpr std::string_view sorted_entries =
        "  - [0, 0.0]\n"
        "  - [128, 6.0]";
    constexpr std::string_view unsorted_entries =
        "  - [128, 6.0]\n"
        "  - [0, 0.0]";
    const auto entries_position = yaml.find(sorted_entries);
    ASSERT_NE(entries_position, std::string::npos);
    yaml.replace(entries_position, sorted_entries.size(), unsorted_entries);
    const TemporaryYaml invalid_yaml(yaml);

    EXPECT_THROW(parseNpeDeviceModelConfig(invalid_yaml.path()), npeException);
}

}  // namespace
}  // namespace tt_npe
