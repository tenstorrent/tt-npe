import glob
import subprocess
import re
import math
from pathlib import Path


def compare_noc_trace_estimated_vs_real_results(filename):
    try:
        # Run the extract.sh script and capture output
        result = subprocess.run(
            ["tt_npe_run", "-w", filename],
            capture_output=True,
            text=True,
            check=True,
        )
        rawtext = result.stdout

        m = re.search("error vs golden .*: (\d*\.\d*)%",rawtext)
        value = 0
        if m:
            value = float(m.group(1))
        else:
            print(f"Could not extract comparison to golden result for {filename}")

        return value

    except subprocess.CalledProcessError as e:
        print(f"Error processing {filename}: {e}")
    except ValueError as e:
        print(f"Could not convert output to float for {filename}: {e}")


def process_noc_traces_in_dir(directory):
    results = []
    pattern = str(Path(directory) / "noctrace*yaml")
    matching_files = glob.glob(pattern)
    for filename in matching_files:
        results.append(compare_noc_trace_estimated_vs_real_results(filename))
    return results

if __name__ == "__main__":
    # Run the processing
    values = process_noc_traces_in_dir("workload/noc_trace_yaml_workloads/")
    avgval = sum(values) / len(values)
    maxval = max(values)

    # print histogram
    bucket_size = 1.0
    for ibucket in range(math.ceil(maxval/bucket_size)):
        bucket = ibucket * bucket_size
        count = 0
        for v in values:
            if v > bucket and v < bucket + bucket_size:
                count += 1
        print(f"{bucket:4.1f}%|","===="*count)

    print(f"--------------------------")
    print(f"average error is {avgval:.2f}%")
    print(f"    max error is {maxval:.2f}%")
