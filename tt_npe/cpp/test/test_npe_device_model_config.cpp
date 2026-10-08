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

std::filesystem::path wormholeModelConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
           "data/device/models/wormhole_b0.yaml";
}

std::filesystem::path quasarModelConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
           "data/device/models/quasar.yaml";
}

constexpr std::string_view kBlackholeNocBlock =
    "noc:\n"
    "  topology: torus\n"
    "  routing: torus\n"
    "  num_nocs: 2\n";

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

std::string blackholeConfigWithNocBlock(std::string_view noc_block) {
    auto yaml = readFile(blackholeModelConfigPath());
    const auto position = yaml.find(kBlackholeNocBlock);
    EXPECT_NE(position, std::string::npos);
    yaml.replace(position, kBlackholeNocBlock.size(), noc_block);
    return yaml;
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

TEST(npeDeviceModelConfigTest, LoadsWormholeConfig) {
    const auto config = parseNpeDeviceModelConfig(wormholeModelConfigPath());

    EXPECT_FLOAT_EQ(config.link_bandwidth, 30.0f);
    EXPECT_FLOAT_EQ(config.eth_bandwidth_per_link, 12.5f);
    EXPECT_EQ(config.dram_channels_per_controller, 2);

    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::WORKER), 28.1f);
    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::DRAM), 23.2f);
    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::ETH), 28.1f);
    EXPECT_FLOAT_EQ(config.injection_rates.at(CoreType::UNDEF), 28.1f);
    EXPECT_FLOAT_EQ(config.absorption_rates.at(CoreType::WORKER), 28.1f);
    EXPECT_FLOAT_EQ(config.absorption_rates.at(CoreType::DRAM), 24.0f);
    EXPECT_FLOAT_EQ(config.absorption_rates.at(CoreType::ETH), 24.0f);
    EXPECT_FLOAT_EQ(config.absorption_rates.at(CoreType::UNDEF), 28.1f);

    const TransferBandwidthTable expected_table = {
        {0, 0.0f},
        {128, 5.5f},
        {256, 10.1f},
        {512, 18.0f},
        {1024, 27.4f},
        {2048, 30.0f},
        {8192, 30.0f}};
    EXPECT_EQ(config.transfer_bandwidth_table, expected_table);

    EXPECT_EQ(config.read_latencies.same_tile, 70);
    EXPECT_EQ(config.read_latencies.same_col, 154);
    EXPECT_EQ(config.read_latencies.same_row, 170);
    EXPECT_EQ(config.read_latencies.diagonal, 270);
    EXPECT_EQ(config.write_latencies.startup, 40);
    EXPECT_EQ(config.write_latencies.cycles_per_hop, 10);
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

std::filesystem::path modelConfigDirectory() {
    return blackholeModelConfigPath().parent_path();
}

std::string socDescriptorWithArch(std::string_view arch_name) {
    return fmt::format(
        R"(
grid:
  x_size: 17
  y_size: 12
dram:
  - [0-0, 0-1, 0-11]
eth: [1-1]
functional_workers: [1-2]
router_only: [1-0]
arch_name: {}
)",
        arch_name);
}

TEST(npeDeviceModelConfigTest, ResolvesLowercaseBlackholeArch) {
    const TemporaryYaml soc_descriptor(socDescriptorWithArch("blackhole"));

    const auto resolved =
        resolveNpeDeviceModelConfig(soc_descriptor.path(), modelConfigDirectory());

    EXPECT_EQ(resolved.arch, DeviceArch::Blackhole);
    EXPECT_EQ(resolved.soc_descriptor.arch_name, "blackhole");
    EXPECT_EQ(resolved.soc_descriptor.grid_x_size, 17);
    EXPECT_EQ(resolved.soc_descriptor.grid_y_size, 12);
    EXPECT_EQ(resolved.model_config_path.filename(), "blackhole.yaml");
    EXPECT_FLOAT_EQ(resolved.model_config.link_bandwidth, 60.9f);
}

TEST(npeDeviceModelConfigTest, ResolvesUppercaseWormholeB0Arch) {
    const TemporaryYaml soc_descriptor(socDescriptorWithArch("WORMHOLE_B0"));

    const auto resolved =
        resolveNpeDeviceModelConfig(soc_descriptor.path(), modelConfigDirectory());

    EXPECT_EQ(resolved.arch, DeviceArch::WormholeB0);
    EXPECT_EQ(resolved.soc_descriptor.arch_name, "WORMHOLE_B0");
    EXPECT_EQ(resolved.model_config_path.filename(), "wormhole_b0.yaml");
    EXPECT_FLOAT_EQ(resolved.model_config.link_bandwidth, 30.0f);
}

TEST(npeDeviceModelConfigTest, ResolvesLegacyWormholeArchAlias) {
    const TemporaryYaml soc_descriptor(socDescriptorWithArch("WORMHOLE"));

    const auto resolved =
        resolveNpeDeviceModelConfig(soc_descriptor.path(), modelConfigDirectory());

    EXPECT_EQ(resolved.arch, DeviceArch::WormholeB0);
    EXPECT_EQ(resolved.model_config_path.filename(), "wormhole_b0.yaml");
}

TEST(npeDeviceModelConfigTest, NormalizesArchNameCaseAndWhitespace) {
    const TemporaryYaml soc_descriptor(socDescriptorWithArch("\" BLACKHOLE\\n\""));

    const auto resolved =
        resolveNpeDeviceModelConfig(soc_descriptor.path(), modelConfigDirectory());

    EXPECT_EQ(resolved.arch, DeviceArch::Blackhole);
}

TEST(npeDeviceModelConfigTest, FindsBundledModelConfigDirectory) {
    const TemporaryYaml soc_descriptor(socDescriptorWithArch("blackhole"));

    const auto resolved = resolveNpeDeviceModelConfig(soc_descriptor.path());

    EXPECT_EQ(resolved.model_config_path.filename(), "blackhole.yaml");
    EXPECT_TRUE(std::filesystem::is_regular_file(resolved.model_config_path));
}

TEST(npeDeviceModelConfigTest, RejectsUnsupportedArch) {
    const TemporaryYaml soc_descriptor(socDescriptorWithArch("unknown_arch"));

    EXPECT_THROW(
        resolveNpeDeviceModelConfig(soc_descriptor.path(), modelConfigDirectory()),
        npeException);
}

TEST(npeDeviceModelConfigTest, RejectsInvalidExplicitDirectory) {
    const TemporaryYaml soc_descriptor(socDescriptorWithArch("blackhole"));
    const auto missing_directory =
        std::filesystem::temp_directory_path() /
        fmt::format(
            "tt_npe_missing_model_configs_{}",
            std::chrono::steady_clock::now().time_since_epoch().count());

    EXPECT_THROW(
        resolveNpeDeviceModelConfig(soc_descriptor.path(), missing_directory),
        npeException);
}

TEST(npeDeviceModelConfigTest, LoadsExplicitTorusNocConfig) {
    for (const auto& path : {blackholeModelConfigPath(), wormholeModelConfigPath()}) {
        const auto noc = parseNpeDeviceModelConfig(path).noc;
        EXPECT_EQ(noc.topology, NocTopology::Torus);
        EXPECT_EQ(noc.routing, NocRouting::Torus);
        EXPECT_EQ(noc.num_nocs, 2);
    }
}

TEST(npeDeviceModelConfigTest, DefaultsToTorusNocConfigWhenBlockIsMissing) {
    const TemporaryYaml legacy_yaml(blackholeConfigWithNocBlock(""));
    const auto noc = parseNpeDeviceModelConfig(legacy_yaml.path()).noc;

    EXPECT_EQ(noc.topology, NocTopology::Torus);
    EXPECT_EQ(noc.routing, NocRouting::Torus);
    EXPECT_EQ(noc.num_nocs, 2);
}

TEST(npeDeviceModelConfigTest, LoadsQuasarMeshNocConfig) {
    const auto noc = parseNpeDeviceModelConfig(quasarModelConfigPath()).noc;

    EXPECT_EQ(noc.topology, NocTopology::Mesh);
    EXPECT_EQ(noc.routing, NocRouting::XY);
    EXPECT_EQ(noc.num_nocs, 1);
}

TEST(npeDeviceModelConfigTest, ParsesNocConfigCaseInsensitively) {
    const TemporaryYaml yaml(blackholeConfigWithNocBlock(
        "noc:\n"
        "  topology: Mesh\n"
        "  routing: XY\n"
        "  num_nocs: 1\n"));
    const auto noc = parseNpeDeviceModelConfig(yaml.path()).noc;

    EXPECT_EQ(noc.topology, NocTopology::Mesh);
    EXPECT_EQ(noc.routing, NocRouting::XY);
    EXPECT_EQ(noc.num_nocs, 1);
}

TEST(npeDeviceModelConfigTest, RejectsInvalidNocConfig) {
    constexpr std::string_view invalid_blocks[] = {
        // xy routing on a torus
        "noc:\n  topology: torus\n  routing: xy\n  num_nocs: 1\n",
        // xy routing with two NoCs
        "noc:\n  topology: mesh\n  routing: xy\n  num_nocs: 2\n",
        // torus routing on a mesh
        "noc:\n  topology: mesh\n  routing: torus\n  num_nocs: 2\n",
        // torus routing with one NoC
        "noc:\n  topology: torus\n  routing: torus\n  num_nocs: 1\n",
        // unknown routing and topology
        "noc:\n  topology: torus\n  routing: yx\n  num_nocs: 2\n",
        "noc:\n  topology: ring\n  routing: torus\n  num_nocs: 2\n",
        // missing field and non-map block
        "noc:\n  topology: torus\n  routing: torus\n",
        "noc: torus\n",
    };
    for (const auto block : invalid_blocks) {
        const TemporaryYaml invalid_yaml(blackholeConfigWithNocBlock(block));
        EXPECT_THROW(parseNpeDeviceModelConfig(invalid_yaml.path()), npeException) << block;
    }
}

}  // namespace
}  // namespace tt_npe
