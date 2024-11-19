#pragma once
#include <vector>

namespace tt_npe {

enum class nocLinkDir {
  NOC1_NORTH = 0,
  NOC1_WEST = 1,
  NOC0_EAST = 2,
  NOC0_SOUTH = 3
};

struct nocLink {
  const float bandwidth = 32;
};

class nocNode {
public:
  nocLink &getLink(nocLinkDir dir) { return links[size_t(dir)]; }

private:
  std::vector<nocLink> links;
};

} // namespace tt_npe