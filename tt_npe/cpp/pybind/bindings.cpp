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

    py::class_<tt_npe::npeStats> stats(
        m,
        "Stats",
        "Object containing stats and other information from npe simulation **if sim succeeds**.");
    stats.def_readwrite("completed", &tt_npe::npeStats::completed)
        .def_readwrite("estimated_cycles", &tt_npe::npeStats::estimated_cycles)
        .def_readwrite("estimated_cong_free_cycles", &tt_npe::npeStats::estimated_cong_free_cycles)
        .def_readwrite("golden_cycles", &tt_npe::npeStats::golden_cycles)
        .def_readwrite("cycle_prediction_error", &tt_npe::npeStats::cycle_prediction_error)
        .def_readwrite("num_timesteps", &tt_npe::npeStats::num_timesteps)
        .def_readwrite("wallclock_runtime_us", &tt_npe::npeStats::wallclock_runtime_us)
        .def_readwrite("overall_avg_link_demand", &tt_npe::npeStats::overall_avg_link_demand)
        .def_readwrite("overall_max_link_demand", &tt_npe::npeStats::overall_max_link_demand)
        .def_readwrite("overall_avg_niu_demand", &tt_npe::npeStats::overall_avg_niu_demand)
        .def_readwrite("overall_max_niu_demand", &tt_npe::npeStats::overall_max_niu_demand)
        .def_readwrite("overall_avg_link_util", &tt_npe::npeStats::overall_avg_link_util)
        .def_readwrite("overall_max_link_util", &tt_npe::npeStats::overall_max_link_util)
        .def_readwrite("dram_bw_util", &tt_npe::npeStats::dram_bw_util)
        .def(
            "getCongestionImpact",
            &tt_npe::npeStats::getCongestionImpact,
            "Returns congestion impact")
        .def(
            "__repr__",
            [](const tt_npe::npeStats& stats) -> std::string { return stats.to_string(true); })
        .def("__str__", [](const tt_npe::npeStats& stats) -> std::string {
            return stats.to_string(true);
        });

    // pickle support for npeStats
    stats.def(py::pickle(
        [](const tt_npe::npeStats& stats) {
            return py::make_tuple(
                stats.completed,
                stats.estimated_cycles,
                stats.estimated_cong_free_cycles,
                stats.golden_cycles,
                stats.cycle_prediction_error,
                stats.num_timesteps,
                stats.wallclock_runtime_us,
                stats.overall_avg_link_demand,
                stats.overall_max_link_demand,
                stats.overall_avg_niu_demand,
                stats.overall_max_niu_demand,
                stats.overall_avg_link_util,
                stats.overall_max_link_util,
                stats.dram_bw_util);
        },
        [](py::tuple t) {
            if (t.size() != 14) {
                throw std::runtime_error("Invalid state!");
            }
            tt_npe::npeStats stats;
            stats.completed = t[0].cast<bool>();
            stats.estimated_cycles = t[1].cast<size_t>();
            stats.estimated_cong_free_cycles = t[2].cast<size_t>();
            stats.golden_cycles = t[3].cast<size_t>();
            stats.cycle_prediction_error = t[4].cast<double>();
            stats.num_timesteps = t[5].cast<size_t>();
            stats.wallclock_runtime_us = t[6].cast<size_t>();
            stats.overall_avg_link_demand = t[7].cast<double>();
            stats.overall_max_link_demand = t[8].cast<double>();
            stats.overall_avg_niu_demand = t[9].cast<double>();
            stats.overall_max_niu_demand = t[10].cast<double>();
            stats.overall_avg_link_util = t[11].cast<double>();
            stats.overall_max_link_util = t[12].cast<double>();
            stats.dram_bw_util = t[13].cast<double>();
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
        .def_readwrite("use_v1_timeline_format", &tt_npe::npeConfig::use_v1_timeline_format)
        .def_readwrite("estimate_cong_impact", &tt_npe::npeConfig::estimate_cong_impact)
        .def_readwrite(
            "infer_injection_rate_from_src", &tt_npe::npeConfig::infer_injection_rate_from_src)
        .def_readwrite("cluster_coordinates_json", &tt_npe::npeConfig::cluster_coordinates_json)
        .def("set_verbosity_level", &tt_npe::npeConfig::setVerbosityLevel)
        .def("__repr__", &tt_npe::npeConfig::to_string)
        .def("__str__", &tt_npe::npeConfig::to_string);

    //---- misc type bindings -------------------------------------------------
    py::class_<tt_npe::Coord> coord(
        m, "Coord", "The tt-npe Coordinate type; a tuple of {device_id, row, col}. *NOT an {X,Y} pair*!");
    coord.def(py::init<int, int, int>(), py::arg("device_id"), py::arg("row"), py::arg("col"));
    
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

    //---- JSON workload ingestion bindings -----------------------------------
    m.def(
        "createWorkloadFromJSON",
        &tt_npe::createWorkloadFromJSON,
        py::arg("json_wl_filename") = "",
        py::arg("is_noc_trace_format") = false,
        py::arg("verbose") = false,
        "Returns an `npe.Workload` object from a pre-defined workload in a JSON file. If using a "
        "raw tt-metal profiler noc trace, set 'is_noc_trace_format' True ");
}
