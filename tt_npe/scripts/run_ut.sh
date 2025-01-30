# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

BOLD="\033[1m"
BLACK="\033[30m"
RED="\033[31m"
GREEN="\033[32m"
RESET="\033[0m"

show_log_if_fails() {
	test_name=$1
	shift

    local output
    output=$(eval "$@" 2>&1)
    local status=$?
    
    if [ $status -ne 0 ]; then
		printf "\n${BOLD}$test_name ${RED}FAIL${RESET}\n"
        echo "$output" | sed 's/^/| /'
        exit $status
    else
		printf "\n${BOLD}$test_name ${GREEN}PASS${RESET}\n"
    fi
}

# find root dir of repo, then cd to tt_npe/
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR
ROOT=$(git rev-parse --show-toplevel)
echo "Inferring repo root to be '$ROOT'"
cd $ROOT/tt_npe/

show_log_if_fails "Python Unit Tests ..." pytest --color=yes 

show_log_if_fails "C++ Unit Tests ......" ../build/tt_npe/tt_npe_ut --gtest_color=yes 

printf "\n${GREEN}${BOLD}All tests pass!${RESET}\n"
