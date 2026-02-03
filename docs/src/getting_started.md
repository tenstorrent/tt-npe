# Getting Started with tt-npe

## Quick Start

### Install 
```shell
git clone git@github.com:tenstorrent/tt-npe.git
cd tt-npe/ 
./build-npe.sh
```

### Update
```shell
cd tt-npe/ && git pull && ./build-npe.sh
```

### Setup

> [!NOTE]
> `ENV_SETUP` must be `source`'d **after** sourcing tt-metal Python virtualenv setup 

```shell
source ENV_SETUP
```


# tt-npe Profiler Mode 

### Demo - NoC Simulation Timeline Visualization 
<video src="https://github.com/user-attachments/assets/b1ada0ef-da19-477d-ba0e-be8287cf023e
" width="600" controls></video>

tt-npe Profiler Mode simulates noc behavior using real noc trace data collected
by metal's kernel profiler.  This allows gathering different statistics (overall
NoC and DRAM utilization, NoC congestion impact, etc) as well as generating a
detailed timeline of noc events that can be visualized with the ttnn-visualizer.

### ttnn NoC Trace Integration
tt-metal device profiler can collect detailed traces of all noc events for
analysis by tt-npe. This will work out of the box for _regular ttnn models/ops_.

> [!IMPORTANT]
> Set `TT_METAL_PROFILER_PROGRAM_SUPPORT_COUNT=10000` when running NoC tracing. This provides enough room on device to store all the NoC event data in DRAM for a small op graph.

> [!TIP]
> Minimize the number of ops profiled at once. The number of ops that can be sampled before running out of device DRAM for events is approximately 5-100 ops. Keep `iterations=1` if you can, or profile different subgraphs separately.

```shell
TT_METAL_PROFILER_PROGRAM_SUPPORT_COUNT=10000 tt_metal/tools/profiler/profile_this.py --collect-noc-traces -c 'pytest command/to/trace.py' -o output_dir
```
Tracing will produce multiple outputs, all in the specified output directory:

- Ops perf report CSV in `output_dir/reports/<timestamp>/`, with NoC, DRAM, and Ethernet Utilization columns.
- Raw noc traces in `output_dir/.logs/`.
- ttnn-visualizer timeline files located in `output_dir/reports/<timestamp>/npe_viz/`. 

#### Useful Links
- See [below](#generating-visualizer-timelines-from-noc-traces-using-tt-npe) for more 
info on how to create these from the raw noc traces directly.
- See [noc trace format](https://github.com/tenstorrent/tt-npe/blob/main/tt_npe/doc/noc_trace_format.md) for more details on the format of the noc traces.

### Manually generating ttnn-visualizer timelines from noc files

Running `profile_this.py` or `tracy.py` with noc tracing enabled should produce
a npe_viz/ folder within the output directory automatically. If not, the
visualizer timeline files can be manually generated as follows: 

```shell
npe_analyze_noc_trace_dir.py my_output_directory/.logs/ -e
```

Note these simulation timeline files are _also JSON format internally_, but are
a different format than the noc trace JSON extracted by the metal profiler.

### Visualizing NPE Timelines

> [!IMPORTANT]
> **The visualizer is available as a web-app with no installation required:**
> [https://ttnn-visualizer.tenstorrent.com/npe](https://ttnn-visualizer.tenstorrent.com/npe)

To use the ttnn-visualizer with npe, simply open it and click on the 'NPE' button on the top bar.
Then load a timeline file generated earlier to begin visualizing!

The ttnn visualizer can also be installed and used locally; see
[ttnn-visualizer](https://github.com/tenstorrent/ttnn-visualizer/) for more
details on installation and use.


## Advanced

### NoC Tracing Raw tt-metal Executables 

It is possible to trigger noc trace profiling for raw metal executables (no
TTNN) without using any wrappers.  This is usually not recommeneded, as TTNN
provides useful features (Op IDs), but can be useful for microbenchmarking
analysis, etc.

First compile tt-metal with `./build_metal.sh --enable-profiler ...`
tt-metal executables must call `tt::tt_metal::DumpDeviceProfileResults(device, program)`.

Then run your tt-metal executable with the following env variables set:
```shell
TT_METAL_PROFILER_PROGRAM_SUPPORT_COUNT=10000 TT_METAL_DEVICE_PROFILER_NOC_EVENTS=1 TT_METAL_DEVICE_PROFILER=1 ./path/to/tt-metal/executable
```
The raw noc traces are dumped to `path/to/tt-metal/generated/profiler/.logs/`, and can be
further analyzed without additional profiler runs.

## API 

For API documentation, programmatic workload construction, and developer resources, see the [API and Developer Guide](api.md).
