#!/usr/bin/env python3

import tt_npe_pybind as npe
import argparse
import sys
import os

def parse_cli_args():
    """Create and return an ArgumentParser with the specified options."""
    parser = argparse.ArgumentParser(description="Simulation Configuration Parser")

    # Basic simulation parameters
    parser.add_argument(
        "-c",
        "--cycles-per-timestep",
        type=int,
        default=256,
        help="Number of cycles a simulation timestep spans",
    )

    parser.add_argument(
        "-d",
        "--device",
        type=str,
        default="wormhole_b0",
        help="Name of device to be simulated",
    )

    parser.add_argument(
        "--cong-model",
        type=str,
        default="fast",
        choices=["none", "fast"],
        help="Congestion model to use (options: 'none', 'fast')",
    )

    # Configuration files
    parser.add_argument(
        "-t",
        "--test-config",
        type=str,
        help="If present, configure a test using YAML configuration file",
    )

    parser.add_argument(
        "-w", "--workload", type=str, default="", help="Run workload from YAML file"
    )

    parser.add_argument(
        "--no-injection-rate-inference",
        action="store_true",
        help="Disable injection rate inference based on transfer's src core type (WORKER,DRAM, etc)",
    )

    # Stats output options
    parser.add_argument(
        "-e",
        "--emit-stats-as-json",
        action="store_true",
        help="Emit detailed stats as a JSON file",
    )

    parser.add_argument(
        "--stats-json-filepath",
        type=str,
        default="npe_stats.json",
        help="Filepath for detailed stat json output",
    )

    # Verbose output
    parser.add_argument(
        "-v",
        "--verbose",
        nargs="?",
        const=1,
        type=int,
        default=0,
        help="Enable verbose output",
    )

    return parser.parse_args()


def main():
    args = parse_cli_args()

    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.device_name = args.device
    cfg.congestion_model_name = args.cong_model
    cfg.workload_yaml_filepath = args.workload
    cfg.cycles_per_timestep = args.cycles_per_timestep
    cfg.emit_stats_as_json = args.emit_stats_as_json
    cfg.stats_json_filepath = args.stats_json_filepath
    cfg.infer_injection_rate_from_src = not args.no_injection_rate_inference
    cfg.set_verbosity_level(1 if args.verbose else 0)

    # provide helpful feedback here ahead-of-time about workload file issues
    if cfg.workload_yaml_filepath == "":
        sys.stderr.write(
            f"E: Must provide a tt-npe workload YAML file with option -w,--workload \n"
        )
        sys.exit(1)

    wl = npe.createWorkloadFromYAML(cfg.workload_yaml_filepath)
    if wl is not None:
        print("Loaded workload successfully, starting tt-npe ... ");
        npe_api = npe.InitAPI(cfg)
        if npe_api is None:
            sys.stderr.write(f"E: tt-npe could not be initialized, check that config is sound? \n")
            sys.exit(1)

        result = npe_api.runNPE(wl)
        if type(result) == npe.Stats:
            print(f"tt-npe simulation finished successfully in {result.wallclock_runtime_us} us!");
            print("--- stats ----------------------------------")
            print(result)
        else:
            sys.stderr.write(f"E: tt-npe crashed during perf estimation: {result}\n")
    else:
        sys.stderr.write(f"E: Could not create tt-npe workload from file '{args.workload}'; aborting ... \n")

if __name__ == "__main__":
    main()
