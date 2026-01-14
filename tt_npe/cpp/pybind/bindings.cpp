// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ingestWorkload.hpp"
#include "npeAPI.hpp"

namespace py = pybind11;

PYBIND11_MODULE(tt_npe_pybind, m) {
    m.doc() =
        "Python bindings for tt-npe (NoC perf estimation model)";  // Optional module docstring

    //---- simulation runner API bindings -------------------------------------

    py::class_<tt_npe::npeAPI>(m, "API").def(
        "runNPE",
        [](const tt_npe::npeAPI& api, const tt_npe::npeWorkload& wl) -> tt_npe::npeResult {
            return api.runNPE(wl);
        },
        "Runs a workload sim and obtains a result (`npe.Stats` if successful, `npe.Exception` "
        "otherwise)",
        py::arg("workload"));

    m.def(
        "InitAPI",
        [](const tt_npe::npeConfig& cfg) -> std::optional<tt_npe::npeAPI> {
            try {
                return tt_npe::npeAPI(cfg);
            } catch (const tt_npe::npeException& exp) {
                tt_npe::log_error("{}", exp.what());
                return {};
            }
        },
        "Constructs a new `npe.API` handle given an `npe.Config` object");

    // Bind the nested deviceStats struct first
    py::class_<tt_npe::npeStats::deviceStats> device_stats(
        m,
        "DeviceStats",
        "Per-device statistics from npe simulation.");
    device_stats
        .def_readwrite("completed", &tt_npe::npeStats::deviceStats::completed)
        .def_readwrite("estimated_cycles", &tt_npe::npeStats::deviceStats::estimated_cycles)
        .def_readwrite("estimated_cong_free_cycles", &tt_npe::npeStats::deviceStats::estimated_cong_free_cycles)
        .def_readwrite("golden_cycles", &tt_npe::npeStats::deviceStats::golden_cycles)
        .def_readwrite("cycle_prediction_error", &tt_npe::npeStats::deviceStats::cycle_prediction_error)
        .def_readwrite("wallclock_runtime_us", &tt_npe::npeStats::deviceStats::wallclock_runtime_us)
        .def_readwrite("overall_avg_link_demand", &tt_npe::npeStats::deviceStats::overall_avg_link_demand)
        .def_readwrite("overall_max_link_demand", &tt_npe::npeStats::deviceStats::overall_max_link_demand)
        .def_readwrite("overall_avg_niu_demand", &tt_npe::npeStats::deviceStats::overall_avg_niu_demand)
        .def_readwrite("overall_max_niu_demand", &tt_npe::npeStats::deviceStats::overall_max_niu_demand)
        .def_readwrite("overall_avg_link_util", &tt_npe::npeStats::deviceStats::overall_avg_link_util)
        .def_readwrite("overall_max_link_util", &tt_npe::npeStats::deviceStats::overall_max_link_util)
        .def_readwrite("overall_avg_noc0_link_demand", &tt_npe::npeStats::deviceStats::overall_avg_noc0_link_demand)
        .def_readwrite("overall_avg_noc0_link_util", &tt_npe::npeStats::deviceStats::overall_avg_noc0_link_util)
        .def_readwrite("overall_max_noc0_link_demand", &tt_npe::npeStats::deviceStats::overall_max_noc0_link_demand)
        .def_readwrite("overall_avg_noc1_link_demand", &tt_npe::npeStats::deviceStats::overall_avg_noc1_link_demand)
        .def_readwrite("overall_avg_noc1_link_util", &tt_npe::npeStats::deviceStats::overall_avg_noc1_link_util)
        .def_readwrite("overall_max_noc1_link_demand", &tt_npe::npeStats::deviceStats::overall_max_noc1_link_demand)
        .def_readwrite("dram_bw_util", &tt_npe::npeStats::deviceStats::dram_bw_util)
        .def_readwrite("dram_bw_util_sim", &tt_npe::npeStats::deviceStats::dram_bw_util_sim)
        .def_readwrite("eth_bw_util_per_core", &tt_npe::npeStats::deviceStats::eth_bw_util_per_core)
        .def(
            "getCongestionImpact",
            &tt_npe::npeStats::deviceStats::getCongestionImpact,
            "Returns congestion impact")
        .def(
            "getEthBwUtilPerCoreStr",
            &tt_npe::npeStats::deviceStats::getEthBwUtilPerCoreStr,
            "Returns ETH BW util per core as a formatted string")
        .def(
            "getAggregateEthBwUtil",
            &tt_npe::npeStats::deviceStats::getAggregateEthBwUtil,
            "Returns aggregate (average) ETH BW util across all cores")
        .def(
            "__repr__",
            [](const tt_npe::npeStats::deviceStats& ds) -> std::string { return ds.to_string(true); })
        .def("__str__", [](const tt_npe::npeStats::deviceStats& ds) -> std::string {
            return ds.to_string(true);
        });

    // pickle support for deviceStats
    device_stats.def(py::pickle(
        [](const tt_npe::npeStats::deviceStats& ds) {
            // Convert eth_bw_util_per_core map to a list of tuples for pickling
            py::list eth_bw_list;
            for (const auto& [coord, util] : ds.eth_bw_util_per_core) {
                eth_bw_list.append(py::make_tuple(coord.device_id, coord.row, coord.col, util));
            }
            return py::make_tuple(
                ds.completed,
                ds.estimated_cycles,
                ds.estimated_cong_free_cycles,
                ds.golden_cycles,
                ds.cycle_prediction_error,
                ds.wallclock_runtime_us,
                ds.overall_avg_link_demand,
                ds.overall_max_link_demand,
                ds.overall_avg_niu_demand,
                ds.overall_max_niu_demand,
                ds.overall_avg_link_util,
                ds.overall_max_link_util,
                ds.overall_avg_noc0_link_demand,
                ds.overall_avg_noc0_link_util,
                ds.overall_max_noc0_link_demand,
                ds.overall_avg_noc1_link_demand,
                ds.overall_avg_noc1_link_util,
                ds.overall_max_noc1_link_demand,
                ds.dram_bw_util,
                ds.dram_bw_util_sim,
                eth_bw_list);
        },
        [](py::tuple t) {
            if (t.size() != 21) {
                throw std::runtime_error("Invalid deviceStats pickle state!");
            }
            tt_npe::npeStats::deviceStats ds;
            ds.completed = t[0].cast<bool>();
            ds.estimated_cycles = t[1].cast<size_t>();
            ds.estimated_cong_free_cycles = t[2].cast<size_t>();
            ds.golden_cycles = t[3].cast<size_t>();
            ds.cycle_prediction_error = t[4].cast<double>();
            ds.wallclock_runtime_us = t[5].cast<size_t>();
            ds.overall_avg_link_demand = t[6].cast<double>();
            ds.overall_max_link_demand = t[7].cast<double>();
            ds.overall_avg_niu_demand = t[8].cast<double>();
            ds.overall_max_niu_demand = t[9].cast<double>();
            ds.overall_avg_link_util = t[10].cast<double>();
            ds.overall_max_link_util = t[11].cast<double>();
            ds.overall_avg_noc0_link_demand = t[12].cast<double>();
            ds.overall_avg_noc0_link_util = t[13].cast<double>();
            ds.overall_max_noc0_link_demand = t[14].cast<double>();
            ds.overall_avg_noc1_link_demand = t[15].cast<double>();
            ds.overall_avg_noc1_link_util = t[16].cast<double>();
            ds.overall_max_noc1_link_demand = t[17].cast<double>();
            ds.dram_bw_util = t[18].cast<double>();
            ds.dram_bw_util_sim = t[19].cast<double>();
            // Reconstruct eth_bw_util_per_core from list of tuples
            py::list eth_bw_list = t[20].cast<py::list>();
            for (const auto& item : eth_bw_list) {
                py::tuple coord_tuple = item.cast<py::tuple>();
                tt_npe::Coord coord(
                    coord_tuple[0].cast<int>(),
                    coord_tuple[1].cast<int>(),
                    coord_tuple[2].cast<int>());
                ds.eth_bw_util_per_core[coord] = coord_tuple[3].cast<double>();
            }
            return ds;
        }));

    py::class_<tt_npe::npeStats> stats(
        m,
        "Stats",
        "Object containing stats and other information from npe simulation **if sim succeeds**. "
        "Stats are stored per-device in the `per_device_stats` dictionary.");
    stats
        .def_readwrite("per_device_stats", &tt_npe::npeStats::per_device_stats)
        .def(
            "__repr__",
            [](const tt_npe::npeStats& stats) -> std::string { return stats.to_string(true); })
        .def("__str__", [](const tt_npe::npeStats& stats) -> std::string {
            return stats.to_string(true);
        });

    // pickle support for npeStats
    stats.def(py::pickle(
        [](const tt_npe::npeStats& stats) {
            // Pickle the per_device_stats map
            return py::make_tuple(stats.per_device_stats);
        },
        [](py::tuple t) {
            if (t.size() != 1) {
                throw std::runtime_error("Invalid npeStats pickle state!");
            }
            tt_npe::npeStats stats;  // use default constructor for unpickling
            stats.per_device_stats = t[0].cast<std::unordered_map<tt_npe::DeviceID, tt_npe::npeStats::deviceStats>>();
            return stats;
        }));

    py::class_<tt_npe::npeException> exception(
        m,
        "Exception",
        "Returned from npe.runNPE if an error occurs during simulation (timeout, malformed "
        "workload, etc).");
    exception.def("err", &tt_npe::npeException::what);
    exception.def("__repr__", &tt_npe::npeException::what);
    exception.def("__str__", &tt_npe::npeException::what);

    //---- config bindings ----------------------------------------------------
    py::class_<tt_npe::npeConfig>(
        m,
        "Config",
        "Class containing all configuration needed to initialize an `npe.API` handle object. *All "
        "fields have reasonable defaults*. If reading a workload from a .json file, "
        "workload_json_filepath must be set to a valid filepath.")
        .def(py::init<>())
        .def_readwrite("device_name", &tt_npe::npeConfig::device_name)
        .def_readwrite("congestion_model_name", &tt_npe::npeConfig::congestion_model_name)
        .def_readwrite("workload_json_filepath", &tt_npe::npeConfig::workload_json)
        .def_readwrite("cycles_per_timestep", &tt_npe::npeConfig::cycles_per_timestep)
        .def_readwrite("emit_timeline_file", &tt_npe::npeConfig::emit_timeline_file)
        .def_readwrite("scale_workload_schedule", &tt_npe::npeConfig::scale_workload_schedule)
        .def_readwrite("remove_localized_unicast_transfers", &tt_npe::npeConfig::remove_localized_unicast_transfers)
        .def_readwrite("workload_is_noc_trace", &tt_npe::npeConfig::workload_is_noc_trace)
        .def_readwrite("compress_timeline_output_file", &tt_npe::npeConfig::compress_timeline_output_file)
        .def_readwrite("timeline_filepath", &tt_npe::npeConfig::timeline_filepath)
        .def_readwrite("use_legacy_timeline_format", &tt_npe::npeConfig::use_legacy_timeline_format)
        .def_readwrite("estimate_cong_impact", &tt_npe::npeConfig::estimate_cong_impact)
        .def_readwrite(
            "infer_injection_rate_from_src", &tt_npe::npeConfig::infer_injection_rate_from_src)
        .def_readwrite("topology_json", &tt_npe::npeConfig::topology_json)
        .def("set_verbosity_level", &tt_npe::npeConfig::setVerbosityLevel)
        .def("__repr__", &tt_npe::npeConfig::to_string)
        .def("__str__", &tt_npe::npeConfig::to_string);

    //---- misc type bindings -------------------------------------------------
    py::class_<tt_npe::Coord> coord(
        m, "Coord", "The tt-npe Coordinate type; a tuple of {device_id, row, col}. *NOT an {X,Y} pair*!");
    coord.def(py::init<int, int, int>(), py::arg("device_id"), py::arg("row"), py::arg("col"))
        .def_readwrite("device_id", &tt_npe::Coord::device_id)
        .def_readwrite("row", &tt_npe::Coord::row)
        .def_readwrite("col", &tt_npe::Coord::col);
    
    py::class_<tt_npe::MulticastCoordSet> mcast_coord_pair(
        m, "MulticastCoordSet", "A multicast coordinate pair containing one or more coordinate grids");
    mcast_coord_pair.def(py::init<>())
                   .def(py::init<const tt_npe::Coord&, const tt_npe::Coord&>(), 
                        py::arg("start"), py::arg("end"))
                   .def("grid_size", &tt_npe::MulticastCoordSet::grid_size, 
                        "Returns the total number of coordinates in all grids");


    py::enum_<tt_npe::nocType>(
        m, "NocType", "An enum representing the device NoC used for a Transfer.")
        .value("NOC_0", tt_npe::nocType::NOC0)
        .value("NOC_1", tt_npe::nocType::NOC1);

    //---- workload construction bindings -------------------------------------
    py::class_<tt_npe::npeWorkloadTransfer> transfer(
        m,
        "Transfer",
        "Represents a contiguous series of packet(s) from a single "
        "source to one or more destinations. Roughly 1:1 equivalent to any single call to "
        "`dataflow_api.h:noc_async_(read|write)`.");
    // New constructor that supports NocDestination (can be either Coord or MulticastCoordSet)
    transfer.def(
        py::init<uint32_t, uint32_t, tt_npe::Coord, tt_npe::NocDestination, float, int, tt_npe::nocType>(),
        "Creates a new, fully-initialized `npe.Transfer` object with flexible destination type. Arguments required are (in "
        "order):\n\n"
        " 1. **packet_size** : Size of packet(s) being transferred **in bytes**. Must be greater "
        "than "
        "1. \n\n"
        " 2. **num_packets** : Number of packets to be transferred. Must be 1 or greater.\n\n"
        " 3. **src** : `npe.Coord` specifying {device_id,row,col} location of the source of the transfer.\n\n"
        " 4. **dst** : `npe.NocDestination` specifying the destination(s) of the transfer. Can be created with "
        "`make_unicast_destination()` or `make_multicast_destination()`.\n\n"
        " 5. **injection_rate** : (*inferred by default, can leave as 0*). Override rate at which "
        "packets are injected into network from source in GB/s. \n\n"
        " 6. **phase_cycle_offset** : Earliest possible cycle transfer can start, relative to the "
        "start of its containing npe.Phase.\n\n"
        " 7. **noc_type** : The noc type used (NOC_0,NOC_1).\n\n",
        py::arg("packet_size"),
        py::arg("num_packets"),
        py::arg("src"),
        py::arg("dst"),
        py::arg("injection_rate"),
        py::arg("phase_cycle_offset"),
        py::arg("noc_type"));

    py::class_<tt_npe::npeWorkloadPhase> phase(
        m,
        "Phase",
        "Represents a logical grouping of NoC transfers that have *no mutual dependencies*. A "
        "`npe.Workload` is composed of multiple `npe.Phase`s. For typical workloads, a single "
        "`npe.Phase` can be used to contain all transfers.");
    phase.def(py::init<>(), "Constructs a new empty `npe.Phase`");
    phase.def(
        "addTransfer",
        [](tt_npe::npeWorkloadPhase& phase, const tt_npe::npeWorkloadTransfer& transfer) {
            phase.transfers.push_back(transfer);
        },
        "Adds a pre-constructed `npe.Transfer` object to this phase.");

    py::class_<tt_npe::npeWorkload> workload(
        m,
        "Workload",
        "Represents a complete 'workload' (listing of NoC data transfers between cores on the "
        "device) to analyze. A workload is passed as an argument to `npe.API.runNPE()` to run a "
        "simulation.");
    workload.def(py::init<>(), "Creates a new empty npe.Workload object.");
    workload.def(
        "addPhase", &tt_npe::npeWorkload::addPhase, "Adds an npe.Phase object into this workload.");
    workload.def(
        "setGoldenResultCycles", [](tt_npe::npeWorkload& wl, py::dict golden_cycles_dict) -> void {
            boost::unordered_flat_map<tt_npe::DeviceID, std::pair<tt_npe::CycleCount, tt_npe::CycleCount>> golden_cycles;
            for (const auto& [device_id, golden_cycles_val] : golden_cycles_dict) {
                golden_cycles[device_id.cast<tt_npe::DeviceID>()] = golden_cycles_val.cast<std::pair<tt_npe::CycleCount, tt_npe::CycleCount>>();
            }
            wl.setGoldenResultCycles(golden_cycles);
        },
        "Sets golden cycles (actual time taken) for this workload.");

    //---- JSON workload ingestion bindings -----------------------------------
    m.def(
        "createWorkloadFromJSON",
        &tt_npe::createWorkloadFromJSON,
        py::arg("json_wl_filename") = "",
        py::arg("device_name") = "",
        py::arg("is_noc_trace_format") = false,
        py::arg("verbose") = false,
        "Returns an `npe.Workload` object from a pre-defined workload in a JSON file. If using a "
        "raw tt-metal profiler noc trace, set 'is_noc_trace_format' True ");
}
