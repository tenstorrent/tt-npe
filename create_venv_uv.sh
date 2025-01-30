#!/bin/bash
set -eo pipefail

# allow overriding python command via environment variable
if [ -z "$python_cmd" ]; then
    python_cmd="python3"
else
    echo "using user-specified python: $python_cmd"
fi

# verify python command exists
if ! command -v $python_cmd &> /dev/null; then
    echo "python command not found: $python_cmd"
    exit 1
fi

echo "using '$(which python3)' as python base install"

# set python environment directory
if [ -z "$python_env_dir" ]; then
    python_env_dir=$(pwd)/python_env
fi

export UV_CACHE_DIR=.uv_cache
export VIRTUAL_ENV=./python_venv
uv venv --quiet $VIRTUAL_ENV
uv pip install --quiet -r requirements.txt
