#include "cli_options.hpp"

#include <boost/program_options/option.hpp>
#include <iostream>

#include "boost/program_options/options_description.hpp"
#include "boost/program_options/parsers.hpp"
#include "boost/program_options/variables_map.hpp"
#include "fmt/core.h"
#include "npeConfig.hpp"

namespace po = boost::program_options;

bool parse_options(tt_npe::npeConfig& npe_config, int argc, char** argv) {
    try {
        // Declare the supported options
        po::options_description desc("Allowed options");
        // clang-format off
        desc.add_options()
            ("help", "show help message")
            ("cycles-per-timestep,c", po::value<int>()->default_value(256),                  "Number of cycles a simulation timestep spans")
            ("cong-model",            po::value<std::string>()->default_value("none"),       "Congestion model to use (options: 'none', 'fast')")
            ("workload-config-file,w",  po::value<std::string>()->required(),                "Workload YAML configuration file")
            ("enable-cong-viz",       po::bool_switch()->default_value(false),               "Turn on visualization for congestion per timestep")
            ("verbose,v",             po::value<int>()->default_value(0)->implicit_value(1), "Enable verbose output");
        // clang-format on

        // Allow for multiple occurrences of -v
        po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();

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
        std::string cong_model = vm["cong-model"].as<std::string>();
        std::string yaml_workload_config = vm["workload-config-file"].as<std::string>();
        bool enable_viz = vm["enable-cong-viz"].as<bool>();

        // Validate congestion model
        if (cong_model != "none" && cong_model != "fast") {
            throw po::validation_error(po::validation_error::invalid_option_value, "cong-model");
        }

        // Calculate verbosity level
        auto verbosity_level = std::clamp(vm["verbose"].as<int>(), 0, 3);
        tt_npe::VerbosityLevel verbosity = static_cast<tt_npe::VerbosityLevel>(verbosity_level);

        if (verbosity > tt_npe::VerbosityLevel::Normal) {
            fmt::print("  Verbosity enabled at level: {}\n", static_cast<int>(verbosity));
        }

        // populate npeConfig
        npe_config.congestion_model_name = cong_model;
        npe_config.yaml_workload_config = yaml_workload_config;
        npe_config.cycles_per_timestep = cycles_per_timestep;
        npe_config.enable_visualizations = enable_viz;
        npe_config.verbosity = verbosity;

    } catch (const po::error& e) {
        fmt::print(stderr, "Error occured when parsing options: {}\n", e.what());
        fmt::print(stderr, "Use tt_npe_run --help for usage information\n");
        return false;
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error occured when parsing options : {}\n", e.what());
        return false;
    } catch (...) {
        fmt::print(stderr, "Unknown error occured when parsing options!\n");
        return false;
    }

    return true;
}