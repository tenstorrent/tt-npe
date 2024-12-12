#include <cassert>
#include <cstdint>
#include <vector>

namespace tt_npe {
using npeCheckpointID = uint32_t;

class npeTransferDependencyTracker {
   public:
    npeCheckpointID createCheckpoint(uint32_t total_dep_count) {
        checkpoints.emplace_back(total_dep_count, checkpoints.size());
        return checkpoints.back().id;
    }

    // increments dep counter for checkpoint, returns true if checkpoint has all deps satisfied
    bool updateCheckpoint(npeCheckpointID id) {
        assert(id < checkpoints.size());
        auto& cp = checkpoints[id];
        cp.dep_completed++;
        return cp.done();
    }

   private:
    struct npeCheckpoint {
        npeCheckpoint(uint32_t total_dep_count, npeCheckpointID id) :
            dep_completed(0), dep_total(total_dep_count), id(id) {}
        uint32_t dep_completed;
        uint32_t dep_total;
        bool done() const { return dep_completed == dep_total; }
        npeCheckpointID id;
    };

    std::vector<npeCheckpoint> checkpoints;
};
}  // namespace tt_npe