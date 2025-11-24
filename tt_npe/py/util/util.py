#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC
import sys
import orjson

def log_error(message):
    RED = "\033[91m"
    BOLD = "\033[1m"
    RESET = "\033[0m"
    print(f"{RED}{BOLD}E: {message}{RESET}", file=sys.stderr)

def log_warning(message):
    YELLOW = '\033[93m'
    BOLD = "\033[1m"
    RESET = "\033[0m"
    print(f"{YELLOW}{BOLD}W: {message}{RESET}", file=sys.stderr)

def log_info(message, quiet):
    if quiet: return
    BOLD = "\033[1m"
    RESET = "\033[0m"
    leading_space = len(message) - len(message.lstrip())
    stripped_message = message.strip()
    print(" " * leading_space + f"I: {stripped_message}{RESET}")

def log_debug(message, quiet):
    return
    BLUE = '\033[94m'
    BOLD = "\033[1m"
    RESET = "\033[0m"
    print(f"{BLUE}{BOLD}D: {message}{RESET}", file=sys.stderr)

def read_trace_file_events(trace_file, quiet=False):
    with open(trace_file, "rb") as f:
        events = orjson.loads(f.read())
        log_debug(
            f"Reading trace file '{trace_file}' ({len(events)} noc trace events) ... ",
            quiet
        )
        return events

def write_trace_file_events(events, trace_file, quiet=False):
    log_debug(f"Writing combined trace to '{trace_file}'", quiet)
    with open(trace_file, "wb") as f:
        f.write(orjson.dumps(events, option=orjson.OPT_INDENT_2))
