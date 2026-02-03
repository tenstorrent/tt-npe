# tt-npe API and Developer Guide

## Install Dir Structure
Everything is installed to `tt-npe/install/`, including:
- Shared library (`install/lib/libtt_npe.so`)
- Headers C++ API (`install/include/`)
- Python CLI using pybind11 (`install/bin/tt_npe.py`)
- C++ CLI (`install/bin/tt_npe_run`)

## Unit Tests
tt-npe has two unit test suites; one for C++ code and one for Python.

### Run All Tests

```
$ tt_npe/scripts/run_ut.sh # can be run from any pwd
```

## Simulating Predefined Workloads Using tt_npe.py

### Environment Setup
Run the following to add tt-npe install dir to your `$PATH`:

```shell
cd tt-npe/
source ENV_SETUP # add <tt_npe_root>/install/bin/ to $PATH
```

Now run the following:
```shell
tt_npe.py -w tt_npe/workload/example_wl.json
```

**Note**:
- the `-w` argument is *required*, and specifies the JSON workload file to load.
- for a multichip workload, the individual device traces have to be merged and
pre-processed using the `fabric_post_process.py` script

### Other Important Options

Bandwidth derating caused by congestion between concurrent transfers is
modelled **by default**. Congestion modelling can be *disabled* using
`--cong-model none`.

The `-e` option dumps detailed information about simulation timeline (e.g.
congestion and transfer state for each timestep) into a JSON file located at
`npe_timeline.json` (by default). This timeline files can be used with TT-NN
Visualizer to visualize the the congestion and transfer states.

See `tt_npe.py --help` for more information about available options.

## Constructing Workloads Programmatically

tt-npe workloads are comprimised as collections of `Transfers`. Each `Transfer`
represents a *series of back-to-back packets from one source to one or more
destinations*. This is roughly equivalent to a single call to the dataflow APIs
`noc_async_read` and `noc_async_write`.

`Transfers` are grouped *hierarchically* (see diagram). Each workload is a
collection of `Phases`, and each `Phase` is a group of `Transfers`.

![tt-npe workload hierarchy diagram](img/npe_workload_diag.png)

__For most modelling scenarios, putting all `Transfers` in a single monolithic
`Phase` is the correct approach__. The purpose of multiple `Phases` is to
express data dependencies and synchronization common in real workloads. However
full support for this is not yet complete.

### *Example - Constructing a Random Workload using Python API*

See the example script `install/bin/programmatic_workload_generation.py`
for an annotated example of generating and simulating a tt-npe NoC workload via
Python bindings.

## Python API Documentation

Open `tt_npe/doc/tt_npe_pybind.html` to see full documentation of the tt-npe Python API.

## C++ API
The C++ API requires:
1. Including the header `install/include/npeAPI.hpp`
2. Linking to the shared lib `libtt_npe.so`

### Reference Example : C++ CLI (tt_npe_run)
See the example C++ based CLI source code within `tt-npe/tt_npe/cli/`. This
links `libtt_npe.so` as a shared library, and serves as a reference for
interacting with the API.
