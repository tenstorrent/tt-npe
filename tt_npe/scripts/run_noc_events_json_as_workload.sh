#!/bin/bash

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

# get directory of this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

TRACE_FILE=$1
shift

# get basename without file ext of trace file 
BASENAME=$(basename $TRACE_FILE .json)
TMP_WORKLOAD_FILE="wl_${BASENAME}.json"

$SCRIPT_DIR/convert_noc_events_to_workload.py ${TRACE_FILE} ${TMP_WORKLOAD_FILE}

tt_npe_run -w ${TMP_WORKLOAD_FILE} $@

rm -f ${TMP_WORKLOAD_FILE}