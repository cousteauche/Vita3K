// VITA3K_NO_GUI: Performance optimization implementation
#ifdef COREY_BUILD

#include <corey/corey_perf.h>

namespace corey {
    std::atomic<bool> game_is_running{false};
    std::atomic<bool> skip_heavy_ui{true};
    std::atomic<bool> minimal_overlay{true};
}

#endif // COREY_BUILD
