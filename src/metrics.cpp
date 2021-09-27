#include "metrics.hpp"

namespace judge {
namespace metrics {
std::shared_ptr<prometheus::Registry> global_registry() {
    static std::shared_ptr<prometheus::Registry> registry = std::make_shared<prometheus::Registry>();
    return registry;
}
}  // namespace metrics
}  // namespace judge
