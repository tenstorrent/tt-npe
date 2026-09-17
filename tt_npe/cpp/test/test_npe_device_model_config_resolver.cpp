// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "gtest/gtest.h"
#include "npeDeviceModelConfigResolver.hpp"

namespace tt_npe {
namespace {

constexpr std::string_view blackholeSocDescriptor = R"(
grid:
  x_size: 17
  y_size: 12
dram:
  - [0-0, 0-1, 0-11]
eth: [1-1]
functional_workers: [1-2]
router_only: [1-0]
arch_name: blackhole
)";

class TemporarySocDescriptor {
   public:
    explicit TemporarySocDescriptor(std::string_view contents) {
        static std::atomic<uint64_t> sequence = 0;
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                fmt::format(
                    "tt_npe_soc_descriptor_{}_{}.yaml",
                    timestamp,
                    sequence.fetch_add(1));
        std::ofstream output(path_);
        output << contents;
    }

    ~TemporarySocDescriptor() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

   private:
    std::filesystem::path path_;
};

std::filesystem::path modelConfigDirectory() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
           "data/device/models";
}

TEST(npeDeviceModelConfigResolverTest, ResolvesLowercaseBlackholeArch) {
    const TemporarySocDescriptor soc_descriptor(blackholeSocDescriptor);

    const auto resolved =
        resolveNpeDeviceModelConfig(soc_descriptor.path(), modelConfigDirectory());

    EXPECT_EQ(resolved.soc_descriptor.arch_name, "blackhole");
    EXPECT_EQ(resolved.soc_descriptor.grid_x_size, 17);
    EXPECT_EQ(resolved.soc_descriptor.grid_y_size, 12);
    EXPECT_EQ(resolved.model_config_path.filename(), "blackhole.yaml");
    EXPECT_FLOAT_EQ(resolved.model_config.link_bandwidth, 60.9f);
}

TEST(npeDeviceModelConfigResolverTest, NormalizesArchNameCaseAndWhitespace) {
    EXPECT_EQ(normalizeDeviceArchName(" BLACKHOLE\n"), "blackhole");
}

TEST(npeDeviceModelConfigResolverTest, RejectsArchWithoutNpeConfig) {
    auto quasar_soc = std::string(blackholeSocDescriptor);
    const auto arch_position = quasar_soc.find("blackhole");
    ASSERT_NE(arch_position, std::string::npos);
    quasar_soc.replace(arch_position, std::string_view("blackhole").size(), "quasar");
    const TemporarySocDescriptor soc_descriptor(quasar_soc);

    EXPECT_THROW(
        resolveNpeDeviceModelConfig(soc_descriptor.path(), modelConfigDirectory()),
        npeException);
}

}  // namespace
}  // namespace tt_npe
