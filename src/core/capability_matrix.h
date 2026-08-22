#pragma once

#include <string>
#include <vector>

namespace gno {

struct CapabilityEntry {
    std::string title;
    std::string detail;
};

// Honest capability matrix: what GNO can change on this machine right now,
// and what it deliberately cannot or will not do.
class CapabilityMatrix {
public:
    static std::vector<CapabilityEntry> canDo(bool elevated);
    static std::vector<CapabilityEntry> cannotDo();
};

} // namespace gno
