// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "gtest/gtest.h"
#include "npeAPI.hpp"
#include "npeConfig.hpp"
#include "npeDeviceModelFactory.hpp"

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer) || \
    __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define TT_NPE_PERF_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define TT_NPE_PERF_SANITIZED 1
#endif

namespace tt_npe {
namespace {

// Wall-clock budgets are only meaningful for optimized, uninstrumented builds.
// The Release build does not define NDEBUG, so check the optimizer instead.
#if !defined(__OPTIMIZE__) || defined(TT_NPE_PERF_SANITIZED)
#define SKIP_UNLESS_OPTIMIZED_BUILD() \
    GTEST_SKIP() << "runtime regression tests require an optimized, non-sanitized build"
#else
#define SKIP_UNLESS_OPTIMIZED_BUILD() static_cast<void>(0)
#endif

constexpr int kNumRuns = 5;
constexpr int kNumTransfers = 5000;
constexpr Cycle kScheduleSpanCycles = 1'000'000;

// Simulation-loop budget for the Blackhole benchmark workload. Both models
// measured ~105 ms on a dev server when this test was added; the headroom
// absorbs slower CI runners while still catching large regressions in shared
// hot-path code (updateSimulationStats, modelCongestion).
constexpr size_t kBlackholeSimLoopBudgetUs = 250'000;

// Models that do the same per-timestep work should stay close to each other.
constexpr double kMaxRuntimeRatio = 1.25;

std::filesystem::path dataDirectory() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data";
}

std::filesystem::path blackholeLayout() {
    return dataDirectory() / "device/layout/arch-blackhole.yaml";
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path);
    output << contents;
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto position = text.find(from);
    EXPECT_NE(position, std::string::npos) << "missing '" << from << "'";
    if (position != std::string::npos) {
        text.replace(position, from.size(), to);
    }
    return text;
}

// A Quasar mesh with Blackhole's grid, workers, bandwidths, and latencies, so
// that the only difference from the Blackhole torus is the NoC model.
class BlackholeShapedQuasarMesh {
   public:
    BlackholeShapedQuasarMesh() {
        directory_ = std::filesystem::temp_directory_path() /
                     fmt::format(
                         "tt_npe_perf_mesh_{}",
                         std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(directory_ / "models");

        soc_descriptor_ = directory_ / "soc_descriptor.yaml";
        writeFile(
            soc_descriptor_,
            replaceOnce(
                readFile(blackholeLayout()), "arch_name: BLACKHOLE", "arch_name: quasar"));
        writeFile(
            directory_ / "models/quasar.yaml",
            replaceOnce(
                readFile(dataDirectory() / "device/models/blackhole.yaml"),
                "  topology: torus\n  routing: torus\n  num_nocs: 2\n",
                "  topology: mesh\n  routing: xy\n  num_nocs: 1\n"));
    }

    ~BlackholeShapedQuasarMesh() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    const std::filesystem::path& socDescriptor() const { return soc_descriptor_; }
    std::filesystem::path modelConfigDirectory() const { return directory_ / "models"; }

   private:
    std::filesystem::path directory_;
    std::filesystem::path soc_descriptor_;
};

class ScopedEnvironmentVariable {
   public:
    ScopedEnvironmentVariable(const char* name, const std::string& value) : name_(name) {
        if (const char* previous = std::getenv(name)) {
            previous_ = previous;
        }
        setenv(name, value.c_str(), 1);
    }

    ~ScopedEnvironmentVariable() {
        if (previous_.has_value()) {
            setenv(name_, previous_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

   private:
    const char* name_;
    std::optional<std::string> previous_;
};

std::vector<Coord> workerCoords(const npeDeviceModel& model) {
    std::vector<Coord> workers;
    for (size_t row = 0; row < model.getRows(); ++row) {
        for (size_t col = 0; col < model.getCols(); ++col) {
            const Coord coord{0, static_cast<int>(row), static_cast<int>(col)};
            if (model.getCoreType(coord) == CoreType::WORKER) {
                workers.push_back(coord);
            }
        }
    }
    return workers;
}

// Deterministic mix of unicast and multicast writes between random workers,
// staggered over the schedule so many timesteps have a large live set.
npeWorkload makeBenchmarkWorkload(const npeDeviceModel& model, bool use_noc1) {
    const auto workers = workerCoords(model);
    EXPECT_FALSE(workers.empty());

    std::mt19937 rng(0x5eed);
    std::uniform_int_distribution<size_t> pick_worker(0, workers.size() - 1);
    std::uniform_int_distribution<int> pick_percent(0, 99);
    std::uniform_int_distribution<uint32_t> pick_num_packets(1, 16);
    std::uniform_int_distribution<Cycle> pick_offset(0, kScheduleSpanCycles);
    constexpr uint32_t packet_sizes[] = {512, 1024, 2048, 4096, 8192};
    std::uniform_int_distribution<size_t> pick_packet_size(0, std::size(packet_sizes) - 1);

    npeWorkloadPhase phase;
    phase.transfers.reserve(kNumTransfers);
    for (int i = 0; i < kNumTransfers; ++i) {
        const auto& src = workers[pick_worker(rng)];
        const auto noc_type =
            (use_noc1 && pick_percent(rng) < 50) ? nocType::NOC1 : nocType::NOC0;
        const auto packet_size = packet_sizes[pick_packet_size(rng)];
        const auto num_packets = pick_num_packets(rng);
        const auto offset = pick_offset(rng);

        if (pick_percent(rng) < 10) {
            const auto& a = workers[pick_worker(rng)];
            const auto& b = workers[pick_worker(rng)];
            const Coord start{0, std::min(a.row, b.row), std::min(a.col, b.col)};
            const Coord end{0, std::max(a.row, b.row), std::max(a.col, b.col)};
            phase.transfers.emplace_back(
                packet_size,
                num_packets,
                src,
                MulticastCoordSet(start, end),
                0.0f,
                offset,
                noc_type,
                "WRITE_MULTICAST");
        } else {
            phase.transfers.emplace_back(
                packet_size,
                num_packets,
                src,
                workers[pick_worker(rng)],
                0.0f,
                offset,
                noc_type,
                "WRITE_");
        }
    }

    npeWorkload workload;
    workload.addPhase(std::move(phase));
    workload.setGoldenResultCycles({{0, {0, 1}}});
    return workload;
}

npeConfig makeConfig(const std::string& device_name, const std::filesystem::path& soc = {}) {
    npeConfig cfg;
    cfg.device_name = device_name;
    cfg.congestion_model_name = "fast";
    cfg.soc_descriptor_file = soc.string();
    return cfg;
}

// Returns the fastest simulation-loop time over kNumRuns, in microseconds.
// Taking the minimum rejects scheduling noise from the host.
size_t bestSimLoopRuntimeUs(const npeConfig& cfg, bool use_noc1) {
    const auto model = npeDeviceModelFactory::createDeviceModel(cfg);
    const auto workload = makeBenchmarkWorkload(*model, use_noc1);
    const npeAPI api(cfg);

    size_t best = std::numeric_limits<size_t>::max();
    for (int run = 0; run < kNumRuns; ++run) {
        const auto result = api.runNPE(workload);
        if (!std::holds_alternative<npeStats>(result)) {
            ADD_FAILURE() << "simulation failed: " << std::get<npeException>(result).what();
            return best;
        }
        const auto& stats = std::get<npeStats>(result).per_device_stats.at(MESH_DEVICE);
        best = std::min(best, stats.wallclock_runtime_us);
    }
    return best;
}

}  // namespace

TEST(npePerfTest, BlackholeSimLoopStaysWithinBudget) {
    SKIP_UNLESS_OPTIMIZED_BUILD();

    const auto legacy = bestSimLoopRuntimeUs(makeConfig("blackhole"), true);
    const auto custom = bestSimLoopRuntimeUs(makeConfig("blackhole", blackholeLayout()), true);
    fmt::println("sim loop: legacy blackhole {} us, custom blackhole {} us", legacy, custom);

    EXPECT_LE(legacy, kBlackholeSimLoopBudgetUs);
    EXPECT_LE(custom, kBlackholeSimLoopBudgetUs);
    EXPECT_LE(double(custom) / double(legacy), kMaxRuntimeRatio)
        << "custom model is much slower than the legacy model on the same workload";
}

TEST(npePerfTest, MeshSimLoopKeepsPaceWithTorus) {
    SKIP_UNLESS_OPTIMIZED_BUILD();

    const auto torus = bestSimLoopRuntimeUs(makeConfig("blackhole", blackholeLayout()), false);

    const BlackholeShapedQuasarMesh mesh;
    const ScopedEnvironmentVariable model_dir(
        "TT_NPE_DEVICE_MODEL_CONFIG_DIR", mesh.modelConfigDirectory().string());
    const auto mesh_cfg = makeConfig("quasar", mesh.socDescriptor());
    ASSERT_EQ(npeDeviceModelFactory::createDeviceModel(mesh_cfg)->getNumNocs(), 1);
    const auto mesh_runtime = bestSimLoopRuntimeUs(mesh_cfg, false);
    fmt::println("sim loop: blackhole torus {} us, blackhole-shaped mesh {} us", torus, mesh_runtime);

    EXPECT_LE(double(mesh_runtime) / double(torus), kMaxRuntimeRatio)
        << "mesh routing is much slower than torus routing on the same grid";
}

}  // namespace tt_npe
