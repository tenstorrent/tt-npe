#include "cliOptions.hpp"

#include <boost/program_options/option.hpp>
#include <iostream>

#include "boost/program_options/options_description.hpp"
#include "boost/program_options/parsers.hpp"
#include "boost/program_options/variables_map.hpp"
#include "fmt/core.h"
#include "npeConfig.hpp"
#include "util.hpp"

namespace po = boost::program_options;

namespace tt_npe {

bool parse_options(tt_npe::npeConfig& npe_config, int argc, char** argv) {
    try {
        // Declare the supported options
        po::options_description desc("Allowed options");
        // clang-format off
        desc.add_options()
            ("help", "show help message")
            ("cycles-per-timestep,c",         po::value<int>()->default_value(256),                      "Number of cycles a simulation timestep spans")
            ("device,d",                      po::value<std::string>()->default_value("wormhole_b0"),    "Name of device to be simulated")
            ("cong-model",                    po::value<std::string>()->default_value("fast"),           "Congestion model to use (options: 'none', 'fast')")
            ("test-config,t",                 po::value<std::string>()->default_value(""),               "If present, configure a test using YAML configuration file")
            ("workload,w",                    po::value<std::string>()->default_value(""),               "Run workload from YAML file")
            ("enable-cong-viz",               po::bool_switch()->default_value(false),                   "Turn on visualization for congestion per timestep")
            ("no-injection-rate-inference",   po::bool_switch()->default_value(false),                   "Disable injection rate inference based on transfer's src core type (WORKER,DRAM, etc)")
            ("emit-stats-as-json,e",          po::bool_switch()->default_value(false),                   "Emit detailed stats as a JSON file")
            ("stats-json-filepath",           po::value<std::string>()->default_value("npe_stats.json"), "Filepath for detailed stat json output")
            ("verbose,v",                     po::value<int>()->default_value(0)->implicit_value(1),     "Enable verbose output");
        // clang-format on

        // Allow for multiple occurrences of -v
        po::parsed_options parsed =
            po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();

        po::variables_map vm;
        po::store(parsed, vm);

        if (vm.count("help")) {
            desc.print(std::cout);
            return 0;
        }

        // Check if all required options are provided
        po::notify(vm);

        // Get and validate the values
        int cycles_per_timestep = vm["cycles-per-timestep"].as<int>();
        std::string device_name = vm["device"].as<std::string>();
        std::string cong_model = vm["cong-model"].as<std::string>();
        std::string workload_yaml = vm["workload"].as<std::string>();
        std::string test_config_yaml = vm["test-config"].as<std::string>();
        bool enable_viz = vm["enable-cong-viz"].as<bool>();
        bool infer_injection_rate_from_src = not vm["no-injection-rate-inference"].as<bool>();

        // Calculate verbosity level
        auto verbosity_level = std::clamp(vm["verbose"].as<int>(), 0, 3);
        tt_npe::VerbosityLevel verbosity = static_cast<tt_npe::VerbosityLevel>(verbosity_level);

        if (verbosity > tt_npe::VerbosityLevel::Normal) {
            fmt::print("  Verbosity enabled at level: {}\n", static_cast<int>(verbosity));
        }

        // populate npeConfig
        npe_config.device_name = device_name;
        npe_config.congestion_model_name = cong_model;
        npe_config.workload_yaml = workload_yaml;
        npe_config.test_config_yaml = test_config_yaml;
        npe_config.cycles_per_timestep = cycles_per_timestep;
        npe_config.enable_visualizations = enable_viz;
        npe_config.infer_injection_rate_from_src = infer_injection_rate_from_src;
        npe_config.verbosity = verbosity;
        npe_config.emit_stats_as_json = vm["emit-stats-as-json"].as<bool>();
        npe_config.stats_json_filepath = vm["stats-json-filepath"].as<std::string>();

    } catch (const po::error& e) {
        log_error(
            "Error occured when parsing options:\n\t{}\nUse tt_npe_run --help for usage "
            "information",
            e.what());
        return false;
    } catch (const std::exception& e) {
        log_error("Error occured when parsing options : {}", e.what());
        return false;
    } catch (...) {
        log_error("Unknown error occured when parsing options!");
        return false;
    }

    return true;
}
}  // namespace tt_npe
