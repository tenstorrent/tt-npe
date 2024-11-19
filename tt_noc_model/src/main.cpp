#include "nocPE.hpp"
#include <fmt/printf.h>

using namespace fmt;

int main() {

  // construct a nocWorkload to feed to nocPE
  tt_npe::printDiv("Build Workload");
  tt_npe::nocWorkload wl = tt_npe::genTestWorkload("test");

  // run perf estimation
  tt_npe::printDiv("Run NPE");
  tt_npe::nocPE npe("test");

  return 0;
}