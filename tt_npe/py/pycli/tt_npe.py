#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import argparse
import sys
import os

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "lib"))
import tt_npe_pybind as npe

def parse_cli_args():
    """Create and return an ArgumentParser with the specified options."""
    parser = argparse.ArgumentParser(description="Simulation Configuration Parser")

    # Basic simulation parameters
    parser.add_argument(
        "-c",
        "--cycles-per-timestep",
        type=int,
        default=256,
        help="Number of cycles a simulation timestep comprises (default: 256)",
    )

    parser.add_argument(
        "-d",
        "--device",
        type=str,
        default="wormhole_b0",
        choices=["wormhole_b0"],
        help="Name of device to be simulated (default: wormhole_b0)",
    )

    parser.add_argument(
        "--cong-model",
        type=str,
        default="fast",
        choices=["none", "fast"],
        help="Congestion model to use (default: 'fast', optionally: 'none')",
    )

    parser.add_argument(
        "-w", "--workload", type=str, default="", help="Run workload from JSON file"
    )

    parser.add_argument(
        "--no-injection-rate-inference",
        action="store_true",
        help="Disable injection rate inference based on transfer's src core type (WORKER,DRAM, etc)",
    )

    # Timeline output options
    parser.add_argument(
        "-e",
        "--emit-timeline-file",
        action="store_true",
        help="Emit visualizer timeline to file",
    )

    parser.add_argument(
        "-t",
        "--workload-is-noc-trace",
        action="store_true",
        help="Parse workload file as a tt-metal noc trace format",
    )

    parser.add_argument(
        "--compress-timeline-output-file",
        action="store_true",
        help="Compress visualizer timeline output file using zstd",
    )

    parser.add_argument(
        "--scale-workload-schedule",
        type=float,
        default=0.0,
        help="(experimental) Scale workload schedule by a factor",
    )

    parser.add_argument(
        "--timeline-json-filepath",
        type=str,
        default="npe_timeline.json",
        help="Filepath for visualizer timeline output",
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

def log_error(msg):
    red  = "\u001b[31m"
    bold = "\u001b[1m"
    reset = "\u001b[0m"
    sys.stderr.write(f"{red}{bold}{msg}{reset}\n")

def main():
    args = parse_cli_args()

    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.device_name = args.device
    cfg.congestion_model_name = args.cong_model
    cfg.workload_json_filepath = args.workload
    cfg.workload_is_noc_trace = args.workload_is_noc_trace
    cfg.cycles_per_timestep = args.cycles_per_timestep
    cfg.emit_timeline_file = args.emit_timeline_file
    cfg.timeline_filepath = args.timeline_json_filepath
    cfg.infer_injection_rate_from_src = not args.no_injection_rate_inference
    cfg.scale_workload_schedule = args.scale_workload_schedule
    cfg.compress_timeline_output_file = args.compress_timeline_output_file
    cfg.set_verbosity_level(1 if args.verbose else 0)

    if args.verbose:
        print("------ TT-NPE CONFIG ------")
        print(cfg)
        print("---------------------------")

    # provide helpful feedback here ahead-of-time about workload file issues
    if cfg.workload_json_filepath == "":
        log_error(f"E: Must provide a tt-npe workload JSON file with option -w,--workload")
        sys.exit(1)

    wl = npe.createWorkloadFromJSON(cfg.workload_json_filepath, cfg.workload_is_noc_trace)
    if wl is None:
        log_error(f"E: Could not create tt-npe workload from file '{args.workload}'; aborting ... ")
        sys.exit(1)

    print("Loaded workload successfully, starting tt-npe ... ");
    npe_api = npe.InitAPI(cfg)
    if npe_api is None:
        log_error(f"E: tt-npe could not be initialized, check that config is sound?")
        sys.exit(1)

    # run workload simulation using npe_api handle
    result = npe_api.runNPE(wl)
    match type(result):
        case npe.Stats:
            print(f"tt-npe simulation finished successfully in {result.wallclock_runtime_us} us!");
            print("--- stats ----------------------------------")
            print(result)
        case npe.Exception:
            log_error(f"E: tt-npe exited unsuccessfully: {result}")

if __name__ == "__main__":
    main()
