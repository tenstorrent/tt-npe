#!/bin/bash

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

scripts/convert_noc_events_to_workload.py $1 tmp.yaml #--coalesce_packets
shift

tt_npe_run -w tmp.yaml $@
