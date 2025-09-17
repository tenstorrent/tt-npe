## tt-npe — Lightweight Network-on-Chip Performance Estimator

[![Build Status](https://github.com/bgrady-tt/tt-npe/actions/workflows/build_and_test_ubuntu.yml/badge.svg)](https://github.com/bgrady-tt/tt-npe/actions/workflows/build_and_test_ubuntu.yml)
[![License Check](https://github.com/bgrady-tt/tt-npe/actions/workflows/spdx.yml/badge.svg)](https://github.com/bgrady-tt/tt-npe/actions/workflows/spdx.yml)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/tenstorrent/tt-npe)

<div align="center">

<h1>

[Hardware](https://tenstorrent.com/cards/) | [Documentation](https://docs.tenstorrent.com/tt-npe/) | [Discord](https://discord.gg/tenstorrent) | [Join Us](https://boards.greenhouse.io/tenstorrent?gh_src=22e462047us) 

</h1>

<img src="./img/tt-npe-logo.png" alt="npe logo" height="230"/>

<br>

**tt-npe** is a lightweight Network-on-Chip (NoC) Performance Estimator developed by Tenstorrent to simulate and analyze NoC workloads on Tensix-based devices. It supports profiling, visualization, and integration with tt-metal, enabling developers to optimize NoC communication performance through trace-driven or synthetic simulations. 

tt-npe supports wormhole_b0 and blackhole devices.

</div>

<br>

-----
# Quick Links
* [Getting Started](docs/src/getting_started.md)

-----
# What is this Repo? 
tt-npe simulates the behavior of an abstract NoC "workload" running on an simulated Tenstorrent device. A workload corresponds closely to a trace of all calls to the dataflow_api (i.e. `noc_async_read`, `noc_async_write`, ...). 

tt-npe can also act as a profiler/debugger for NoC traces, integrating with tt-metal profiler's device NoC trace capture feature and the ttnn-visualizer's new NPE mode. For more information, see the  [Profiler Mode](docs/src/getting_started.md#tt-npe-profiler-mode) section of the Getting Started page.

tt-npe can work with:
- Predefined workloads defined in files such as:
    - NoC trace JSON files extracted using tt-metal profiler's noc event capture feature
    - workload files with a simplified format more amenable to generation by other tools
- Programmatically constructed workloads using Python bindings

-----
# Related Tenstorrent Projects
- [tt-forge-fe](https://github.com/tenstorrent/tt-forge-fe)
- [tt-xla](https://github.com/tenstorrent/tt-xla)
- [tt-torch](https://github.com/tenstorrent/tt-torch)
- [tt-mlir](https://github.com/tenstorrent/tt-mlir)
- [tt-metalium](https://github.com/tenstorrent/tt-metal)
- [tt-tvm](https://github.com/tenstorrent/tt-tvm)

<<<<<<< HEAD
## Quick Start

#### Install 
```shell
git clone git@github.com:tenstorrent/tt-npe.git
cd tt-npe/ 
./build-npe.sh
```

#### Update
```shell
cd tt-npe/ && git pull && ./build-npe.sh
```

#### Setup

> [!NOTE]
> `ENV_SETUP` must be `source`'d **after** sourcing tt-metal Python virtualenv setup 

```shell
source ENV_SETUP
```
## tt-npe Profiler Mode 

#### tt-metal noc trace integration
tt_metal device profiler can collect detailed traces of all noc events for
analysis by tt-npe. This will work out of the box for _regular ttnn
models/ops_. Pure tt_metal executables must call
`tt::tt_metal::DumpDeviceProfileResults()`.

```shell
tt_metal/tools/profiler/profile_this.py --collect-noc-traces -c 'pytest command/to/trace.py' -o output_dir
```

tt-npe data should be automatically added to the ops perf report CSV in
`output_dir/reports/`. The new columns corresponding to tt-npe data are `'DRAM
BW UTIL'` and `'NOC UTIL'`.

Additionally, the raw noc traces are dumped to `output_dir/.logs/`, and can be
further analyzed without additional profiler runs.

See [noc trace format](https://github.com/tenstorrent/tt-npe/blob/main/tt_npe/doc/noc_trace_format.md) for more details on the format of the noc traces.

#### Generating visualizer timelines from noc traces using tt-npe

```shell
npe_analyze_noc_trace_dir.py my_output_directory/.logs/ -e
```

ttnn-visualizer JSON inputs are dumped to subdir `output_dir/npe_viz/`. Note
these simulation timeline files are _also JSON format files_, but different
than noc trace JSON.

See [ttnn-visualizer](https://github.com/tenstorrent/ttnn-visualizer/) for more
details on installation and use.

## API and Advanced Use

##### Install Dir Structure
Everything is installed to `tt-npe/install/`, including:
- Shared library (`install/lib/libtt_npe.so`)
- Headers C++ API (`install/include/`)
- Python CLI using pybind11 (`install/bin/tt_npe.py`)

### Unit Tests
tt-npe has two unit test suites; one for C++ code and one for Python.

##### Run All Tests

```
$ tt_npe/scripts/run_ut.sh # can be run from any pwd
```

### Simulating Predefined Workloads Using tt_npe.py 

#### Environment Setup
Run the following to add tt-npe install dir to your `$PATH`:

```shell
cd tt-npe/ 
source ENV_SETUP # add <tt_npe_root>/install/bin/ to $PATH 
```

Now run the following:
```shell
tt_npe.py -w tt_npe/workload/example_wl.json
```

**Note**: the `-w` argument is *required*, and specifies the JSON workload file to load.

##### Other Important Options

Bandwidth derating caused by congestion between concurrent transfers is
modelled **by default**. Congestion modelling can be *disabled* using
`--cong-model none`.

The `-e` option dumps detailed information about simulation timeline (e.g.
congestion and transfer state for each timestep) into a JSON file located at
`npe_timeline.json` (by default). Future work is to load this data into a
visualization tool, but it could be used for ad-hoc analysis as well.  

See `tt_npe.py --help` for more information about available options.

### Constructing Workloads Programmatically

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

##### *Example - Constructing a Random Workload using Python API*

See the example script `install/bin/programmatic_workload_generation.py`
for an annotated example of generating and simulating a tt-npe NoC workload via
Python bindings. 

### Python API Documentation

Open `tt_npe/doc/tt_npe_pybind.html` to see full documentation of the tt-npe Python API. 

### C++ API 
The C++ API requires: 
1. Including the header `install/include/npeAPI.hpp`
2. Linking to the shared lib `libtt_npe.so` 

### Modelling Limitations

tt-npe **does not** currently model the following; features with a * are being prioritized.
- *Blackhole device support
- User defined data dependencies
- Ethernet
- Multichip traffic

[deepwiki]: https://deepwiki.com/tenstorrent/tt-npe
[deepwiki badge]: https://deepwiki.com/badge.svg
=======
-----
# Tenstorrent Bounty Program Terms and Conditions
This repo is a part of Tenstorrent’s bounty program. If you are interested in helping to improve tt-forge, please make sure to read the [Tenstorrent Bounty Program Terms and Conditions](https://docs.tenstorrent.com/bounty_terms.html) before heading to the issues tab. Look for the issues that are tagged with both “bounty” and difficulty level!
- - -
>>>>>>> origin
