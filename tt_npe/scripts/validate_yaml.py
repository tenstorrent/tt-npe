# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

import argparse
import yaml
import sys

def validate_yaml_file(file_path):
    """Checks if the given file is valid YAML."""
    try:
        with open(file_path, 'r') as f:
            yaml.safe_load(f)
        print(f"Success: '{file_path}' is a valid YAML file.")
        return True
    except yaml.YAMLError as e:
        print(f"Error: '{file_path}' is not a valid YAML file.")
        print(f"Details: {e}")
        return False
    except FileNotFoundError:
        print(f"Error: File not found at '{file_path}'.")
        return False
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        return False

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Validate a YAML file.')
    parser.add_argument('file_path', type=str, help='The path to the YAML file to validate.')
    args = parser.parse_args()

    if validate_yaml_file(args.file_path):
        sys.exit(0)
    else:
        sys.exit(1)
