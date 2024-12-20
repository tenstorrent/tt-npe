#include <string>

#include "npeWorkload.hpp"

namespace tt_npe {

std::optional<npeWorkload> ingestYAMLWorkload(const std::string &yaml_wl_filename, bool verbose = false);

}
