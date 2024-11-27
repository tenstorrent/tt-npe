#include <exception>
#include <npeConfig.hpp>
#include <npeStats.hpp>
#include <npeWorkload.hpp>
#include <variant>

#include "magic_enum.hpp"
#include "npeDeviceModel.hpp"
#include "npeEngine.hpp"

namespace tt_npe {

enum class npeErrorCode { UNDEF = 0, WORKLOAD_VALIDATION_FAILED = 1, EXCEEDED_SIM_CYCLE_LIMIT = 2 };

class npeError : std::exception {
   public:
    npeError(const npeError &error) = default;

    npeError(std::string err_str, npeErrorCode err_code) : err_str(err_str), err_code(err_code) {}

    const char *what() {
        format_buf = fmt::format("Error {} : {}", err_str, magic_enum::enum_name(err_code));
        return format_buf.c_str();
    }

    const std::string err_str;
    const npeErrorCode err_code = npeErrorCode::UNDEF;

   private:
    std::string format_buf;
};

// empty result indicates failure
using npeResult = std::variant<npeError, npeStats>;
inline bool result_succeeded(const npeResult &result) { return std::holds_alternative<npeStats>(result); }
inline std::optional<npeError> getErrorFromNPEResult(const npeResult &result) {
    if (result_succeeded(result)) {
        return {};
    } else {
        return std::get<npeError>(result);
    }
}
inline std::optional<npeStats> getStatsFromNPEResult(const npeResult &result) {
    if (result_succeeded(result)) {
        return std::get<npeStats>(result);
    } else {
        return {};
    }
}

class npeAPI {
   public:
    npeAPI() = default;
    npeAPI(const tt_npe::npeAPI &api) = default;
    npeAPI &operator=(const tt_npe::npeAPI &api) = default;
    npeAPI(tt_npe::npeAPI &&api) = default;
    npeAPI &operator=(tt_npe::npeAPI &&api) = default;

    // main constructor 
    static std::optional<npeAPI> makeAPI(const tt_npe::npeConfig &cfg);

    // Run a performance estimation sim and reports back result
    //   if successful, the result will be a npeStats struct
    //   else, the result will be a npeError struct
    tt_npe::npeResult runNPE(const tt_npe::npeWorkload &wl);

    const npeDeviceModel &getDeviceModel() const { return engine.getDeviceModel(); }

   private:

    tt_npe::npeConfig cfg;
    tt_npe::npeEngine engine;
};

}  // namespace tt_npe