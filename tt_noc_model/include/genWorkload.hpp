#include <yaml-cpp/yaml.h>

#include "nocModel.hpp"
#include "nocWorkload.hpp"

tt_npe::nocWorkload
genTestWorkload(const tt_npe::nocModel &model, const YAML::Node &yaml_cfg);