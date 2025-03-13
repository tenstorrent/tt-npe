#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import argparse
import os
import sys
import subprocess
import shutil
from pathlib import Path
from analyze_noc_trace_dir import analyze_noc_traces_in_dir

import tt_npe_pybind as npe

def prompt_yn(prompt):
  while True:
    choice = input(f"{prompt} (y/n): ").lower()
    if choice in ['y', 'yes']:
      return True
    elif choice in ['n', 'no']:
      return False
    else:
      print("Please enter y or n.")

def get_cli_args():
    parser = argparse.ArgumentParser(
        description="Analyzes all JSON noc traces in a directory using tt-npe",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "-c", "--command", help="command to run", default=None
    )
    parser.add_argument(
        "-o", "--output_dir", help="Output result directory", default=None
    )

    return parser.parse_args()



def run_command_with_noc_tracing(command, output_dir):
    mod_env = os.environ.copy()
    mod_env["TT_METAL_DEVICE_PROFILER_NOC_EVENTS"] = "1"
    try:
        Path(output_dir).mkdir(parents=True, exist_ok=True)
    except FileExistsError as e:
        print(f"Error creating {output_dir}: {e}")
        return

    mod_env["TT_METAL_DEVICE_PROFILER_NOC_EVENTS_RPT_PATH"] = os.path.realpath(
        output_dir
    )

    print(f"Running command {command} with NOC tracing enabled")
    split_command = command.split()
    print("command: ",split_command)
    subprocess.run(split_command, env=mod_env)


def main():
    args = get_cli_args()

    if args.output_dir is None:
        print(f"Error: Must specify an output subdirectory with -o <path> !")
        sys.exit(1)

    output_path = Path(args.output_dir)
    if output_path.exists() and not output_path.is_dir():
        print(f"Error: requested output dir {args.output_dir} exists but is not a directory!")
        sys.exit(1)
    elif output_path.is_dir():
        print(f"\nOutput directory '{output_path}' already exists!")
        if prompt_yn("Delete old data before continuing? "):
        	shutil.rmtree(output_path)

    trace_output_subdir = os.path.join(args.output_dir, "noc_traces")
    Path(trace_output_subdir).mkdir(parents=True, exist_ok=True)

    run_command_with_noc_tracing(args.command, trace_output_subdir)

    print("\n-------------------------------------------")
    print(" Post-Processing noc traces with tt-npe ... ")
    print("-------------------------------------------")
    stats_output_subdir = os.path.join(args.output_dir, "npe_stats")
    Path(stats_output_subdir).mkdir(parents=True, exist_ok=True)
    analyze_noc_traces_in_dir(trace_output_subdir, True)

if __name__ == "__main__":
    main()
