#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ingestWorkload.hpp"
#include "npeAPI.hpp"

namespace py = pybind11;

tt_npe::npeWorkloadTransfer createTransfer(
    uint32_t packet_size,
    uint32_t num_packets,
    tt_npe::Coord src,
    tt_npe::Coord dst,
    float injection_rate,
    tt_npe::CycleCount phase_cycle_offset,
    tt_npe::nocType noc_type) {
    return tt_npe::npeWorkloadTransfer(
        packet_size, num_packets, src, dst, injection_rate, phase_cycle_offset, noc_type);
}

PYBIND11_MODULE(tt_npe_pybind, m) {
    m.doc() =
        "Python bindings for tt-npe (NoC perf estimation model)";  // Optional module docstring

    //---- simulation runner API bindings -------------------------------------

    py::class_<tt_npe::npeException> exception(m, "Exception");
    exception.def("err", &tt_npe::npeException::what);
    exception.def("__repr__", &tt_npe::npeException::what);
    exception.def("__str__", &tt_npe::npeException::what);

    py::class_<tt_npe::npeAPI>(m, "API").def(
        "runNPE",
        [](const tt_npe::npeAPI& api, const tt_npe::npeWorkload& wl) -> tt_npe::npeResult {
            return api.runNPE(wl);
        },
        "Run a workload and obtain a result (Stats if successful, Exception otherwise)");

    m.def(
        "InitAPI",
        [](const tt_npe::npeConfig& cfg) -> std::optional<tt_npe::npeAPI> {
            try {
                return tt_npe::npeAPI(cfg);
            } catch (const tt_npe::npeException& exp) {
                tt_npe::log_error("{}",exp.what());
                return {};
            }
        },
        "Construct a new npe.API handle from a Config obj");

    py::class_<tt_npe::npeStats> stats(m, "Stats");
    stats.def_readwrite("completed", &tt_npe::npeStats::completed)
        .def_readwrite("estimated_cycles", &tt_npe::npeStats::estimated_cycles)
        .def_readwrite("num_timesteps", &tt_npe::npeStats::num_timesteps)
        .def_readwrite("wallclock_runtime_us", &tt_npe::npeStats::wallclock_runtime_us)
        .def(
            "__repr__",
            [](const tt_npe::npeStats& stats) -> std::string { return stats.to_string(true); })
        .def("__str__", [](const tt_npe::npeStats& stats) -> std::string {
            return stats.to_string(true);
        });

    //---- config bindings ----------------------------------------------------
    py::class_<tt_npe::npeConfig>(m, "Config")
        .def(py::init<>())
        .def_readwrite("device_name", &tt_npe::npeConfig::device_name)
        .def_readwrite("congestion_model_name", &tt_npe::npeConfig::congestion_model_name)
        .def_readwrite("workload_yaml_filepath", &tt_npe::npeConfig::workload_yaml)
        .def_readwrite("cycles_per_timestep", &tt_npe::npeConfig::cycles_per_timestep)
        .def_readwrite("emit_stats_as_json", &tt_npe::npeConfig::emit_stats_as_json)
        .def_readwrite("stats_json_filepath", &tt_npe::npeConfig::stats_json_filepath)
        .def_readwrite("infer_injection_rate_from_src", &tt_npe::npeConfig::infer_injection_rate_from_src)
        .def("set_verbosity_level", &tt_npe::npeConfig::setVerbosityLevel)
        .def("__repr__", &tt_npe::npeConfig::to_string)
        .def("__str__", &tt_npe::npeConfig::to_string);

    //---- misc type bindings -------------------------------------------------
    py::class_<tt_npe::Coord> coord(m, "Coord", "tt-npe Coordinate type; a pair of {row,col}");
    coord.def(py::init<int, int>());

    py::enum_<tt_npe::nocType>(m, "NocType")
        .value("NOC_0", tt_npe::nocType::NOC0)
        .value("NOC_1", tt_npe::nocType::NOC1);

    //---- workload construction bindings -------------------------------------
    py::class_<tt_npe::npeWorkloadTransfer> transfer(m, "Transfer");
    transfer.def(py::init<>());
    m.def("createTransfer", &createTransfer);

    py::class_<tt_npe::npeWorkloadPhase> phase(m, "Phase");
    phase.def(py::init<>());
    phase.def(
        "addTransfer",
        [](tt_npe::npeWorkloadPhase& phase, const tt_npe::npeWorkloadTransfer& transfer) {
            phase.transfers.push_back(transfer);
        });

    py::class_<tt_npe::npeWorkload> workload(m, "Workload");
    workload.def(py::init<>());
    workload.def("addPhase", &tt_npe::npeWorkload::addPhase);

    //---- YAML workload ingestion bindings -----------------------------------
    m.def(
        "createWorkloadFromYAML",
        &tt_npe::ingestYAMLWorkload,
        py::arg("yaml_wl_filename") = "",
        py::arg("verbose") = false);
}
