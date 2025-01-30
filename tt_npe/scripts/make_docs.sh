#!/bin/bash

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

# check that pdoc3 is installed
if [ -z "$(which pdoc3)" ]; then
    echo "ERROR: pdoc3 is not installed; try running \`pip3 install pdoc3\`"
    exit 1
fi

INSTALL_DIR=$(git rev-parse --show-toplevel)/install/lib/

if [ ! -d "$INSTALL_DIR" ]; then
    echo "ERROR: Could not find installation dir, please build tt-npe first!"
    exit 1
fi

set -x
PYTHONPATH=$INSTALL_DIR pdoc3 --force --html tt_npe_pybind -o doc
