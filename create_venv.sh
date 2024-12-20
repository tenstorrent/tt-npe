#!/bin/bash
set -eo pipefail

# Allow overriding Python command via environment variable
if [ -z "$PYTHON_CMD" ]; then
    PYTHON_CMD="python3"
else
    echo "Using user-specified Python: $PYTHON_CMD"
fi

# Verify Python command exists
if ! command -v $PYTHON_CMD &> /dev/null; then
    echo "Python command not found: $PYTHON_CMD"
    exit 1
fi

echo "Using '$(which python3)'"

# Set Python environment directory
if [ -z "$PYTHON_ENV_DIR" ]; then
    PYTHON_ENV_DIR=$(pwd)/python_env
fi

function create_new_venv_with_pkgs {
    # if needed, create and activate virtual environment
    echo "Creating virtual env in: $PYTHON_ENV_DIR"
    $PYTHON_CMD -m venv $PYTHON_ENV_DIR
    source $PYTHON_ENV_DIR/bin/activate
    
    echo "Installing dev dependencies"
    python3 -m pip install -r requirements.txt
}

echo PYTHON_ENV_DIR = $PYTHON_ENV_DIR

if [ ! -d "$PYTHON_ENV_DIR" ]; then
    create_new_venv_with_pkgs
else
    # if dir exists, try to upgrade it instead
    echo "Attempting to reuse existing PYTHON_ENV_DIR at '$PYTHON_ENV_DIR' ... "
    pip install -r requirements.txt --upgrade
fi
