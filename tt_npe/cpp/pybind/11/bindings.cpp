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
            "__repr__",
            [](const tt_npe::npeStats& stats) -> std::string { return stats.to_string(true); })
        .def("__str__", [](const tt_npe::npeStats& stats) -> std::string {
            return stats.to_string(true);
        });

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
        .def_readwrite("emit_stats_as_json", &tt_npe::npeConfig::emit_stats_as_json)
        .def_readwrite("stats_json_filepath", &tt_npe::npeConfig::stats_json_filepath)
        .def_readwrite(
            "infer_injection_rate_from_src", &tt_npe::npeConfig::infer_injection_rate_from_src)
        .def("set_verbosity_level", &tt_npe::npeConfig::setVerbosityLevel)
        .def("__repr__", &tt_npe::npeConfig::to_string)
        .def("__str__", &tt_npe::npeConfig::to_string);

    //---- misc type bindings -------------------------------------------------
    py::class_<tt_npe::Coord> coord(
        m, "Coord", "The tt-npe Coordinate type; a pair of **{row,col}**. *NOT an {X,Y} pair* !");
    coord.def(py::init<int, int>(), py::arg("row"), py::arg("col"));

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
    transfer.def(
        py::init<uint32_t, uint32_t, tt_npe::Coord, tt_npe::Coord, float, int, tt_npe::nocType>(),
        "Creates a new, fully-initialized `npe.Transfer` object. Arguments required are (in "
        "order):\n\n"
        " 1. **packet_size** : Size of packet(s) being transferred **in bytes**. Must be greater "
        "than "
        "1. \n\n"
        " 2. **num_packets** : Number of packets to be transferred. Must be 1 or greater.\n\n"
        " 3. **src** : `npe.Coord` specifying {row,col} location of the source of the transfer.\n\n"
        " 4. **dst** : `npe.Coord` specifying {row,col} location of the destination of the "
        "transfer.\n\n"
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
        py::arg("noc_type)"));

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
        "getDRAMTrafficStats",
        &tt_npe::npeWorkload::getDRAMTrafficStats,
        "Returns a `npe.DRAMTrafficStats` object containing the total read and write bytes to/from "
        "DRAM in this workload.");

    //---- JSON workload ingestion bindings -----------------------------------
    m.def(
        "createWorkloadFromJSON",
        &tt_npe::ingestJSONWorkload,
        py::arg("json_wl_filename") = "",
        py::arg("verbose") = false,
        "Returns an `npe.Workload` object from a pre-defined workload in a json file.");
}
