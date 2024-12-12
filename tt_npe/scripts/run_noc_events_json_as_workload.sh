#!/bin/bash

scripts/convert_noc_events_to_workload.py $1 tmp.yaml #--coalesce_packets
shift

tt_npe_run -w tmp.yaml $@
