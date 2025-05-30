## tt-npe — Lightweight Network-on-Chip Performance Estimator

[![Build Status](https://github.com/bgrady-tt/tt-npe/actions/workflows/build_and_test_ubuntu.yml/badge.svg)](https://github.com/bgrady-tt/tt-npe/actions/workflows/build_and_test_ubuntu.yml)
[![License Check](https://github.com/bgrady-tt/tt-npe/actions/workflows/spdx.yml/badge.svg)](https://github.com/bgrady-tt/tt-npe/actions/workflows/spdx.yml)

<div align="center">

<h1>

[Hardware](https://tenstorrent.com/cards/) | [Documentation](https://docs.tenstorrent.com/tt-npe/) | [Discord](https://discord.gg/tenstorrent) | [Join Us](https://boards.greenhouse.io/tenstorrent?gh_src=22e462047us) 

</h1>

<img src="./img/tt-npe-logo.png" alt="npe logo" height="230"/>

<br>

**tt-npe** is a lightweight Network-on-Chip (NoC) Performance Estimator developed by Tenstorrent to simulate and analyze NoC workloads on Tensix-based devices. It supports profiling, visualization, and integration with tt-metal, enabling developers to optimize NoC communication performance through trace-driven or synthetic simulations. 

>**NOTE:** tt-npe is currently only available for use with `wormhole_b0`.

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

-----
# Tenstorrent Bounty Program Terms and Conditions
This repo is a part of Tenstorrent’s bounty program. If you are interested in helping to improve tt-forge, please make sure to read the [Tenstorrent Bounty Program Terms and Conditions](https://docs.tenstorrent.com/bounty_terms.html) before heading to the issues tab. Look for the issues that are tagged with both “bounty” and difficulty level!
- - -
